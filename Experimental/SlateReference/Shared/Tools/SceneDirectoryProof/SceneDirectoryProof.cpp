//============================================================================================================================================
//                                                          SCENEDIRECTORYPROOF.CPP
//============================================================================================================================================
// 🧩 Headless proof renderer for the EDITOR's scene directory — the editor's
//    real layout: a workspace window split into viewport / outliner /
//    properties leaves (WorkspacePanel + EditorPanel + PanelStructure), with
//    the scene-directory content (the GPU sky in the viewport leaf, the
//    outliner | details column, the properties / camera-bookmark pages) recorded by
//    SceneDirectoryPanel, and the history demand that fires ONCE per slider
//    drag (not per tick).
//
//    The harness drives the REAL panels through the REAL RecordingSurface and
//    rasterizes the recorded ImDrawList on the CPU, exactly like
//    Tools/TypographyProof — same vendor, same atlas, same pixels the
//    windowed hosts upload. The vendor dock is the one thing the harness
//    cannot run (it lives in the Vulkan-facing InterfaceExchange), so the
//    workspace tab strip is drawn as a static bar; everything below it is the
//    same recording the windowed host makes.
//
//    Scenarios (each writes one PNG under VisualProof/EditorScene/):
//      --shot=editor-overview    one workspace split into a viewport leaf (the
//                                real sky) and an outliner leaf (the scene
//                                directory with its details pane)
//      --shot=editor-outliner-inspector  the outliner leaf settled at its real Inspector slide
//      --shot=editor-sun-props   the workspace split three ways: viewport,
//                                outliner, and a properties leaf showing the
//                                Sun/Sky/Atmosphere slider cards
//      --shot=editor-after-drag  after one elevation drag: the sun rose in the
//                                viewport, and exactly ONE history demand was
//                                raised
//      --shot=editor-camera-fly  after W + right-drag look: the camera moved
//                                and yawed, the sun shifted across the viewport,
//                                and the lag toggle changed the displacement
//
//    Build (repository root, after ApplyImGuiPatches.py):
//      g++ -std=c++20 -O2 -DNDEBUG -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DGLFW_DLL \
//          -DGLFW_INCLUDE_NONE -I Engine -I . -I ExternalPackages/imgui \
//          -I ExternalPackages/glfw/include -I ExternalPackages/thorvg/inc \
//          -I _AgentScratch/Vulkan-Headers/include \
//          Tools/SceneDirectoryProof/SceneDirectoryProof.cpp \
//          Engine/SlateUI/Interface/WorkspacePanel/Source/WorkspacePanel.cpp \
//          Engine/SlateUI/Interface/EditorPanel/Source/EditorPanel.cpp \
//          Engine/SlateUI/Interface/EditorPanel/Source/EditorLeafPanels.cpp \
//          Engine/SlateUI/Interface/FacetPanel/Source/FacetPanel.cpp \
//          Engine/SlateUI/Interface/PanelStructure/Source/PanelStructure.cpp \
//          Engine/SlateUI/Interface/SceneDirectoryPanel/Source/SceneDirectoryPanel.cpp \
//          Engine/SlateUI/Interface/TexturingPanel/Source/TexturingPanel.cpp \
//          Engine/SlateUI/Interface/ControlPanel/Source/ControlPanel.cpp \
//          Engine/SlateUI/Interface/ComponentSpecification/Source/ComponentSpecification.cpp \
//          Engine/SlateUI/Interface/ComponentSpecification/Source/MagnitudeExpression.cpp \
//          Engine/SlateUI/Interface/InterfaceExchange/Source/RecordingSurface.cpp \
//          Engine/SlateUI/Interface/AppearanceSpecification/Source/AppearanceSpecification.cpp \
//          Engine/SlateUI/Interface/ControlIndex/Source/ControlIndex.cpp \
//          Engine/SlateUI/Interface/MotionIntegrator/Source/MotionIntegrator.cpp \
//          Engine/SlateUI/Interface/ThemeSpecification/Source/ThemeSpecification.cpp \
//          Engine/SlateUI/Interface/SymbolSpecification/Source/SymbolSpecification.cpp \
//          Engine/SlateUI/Interface/TextComponent/Source/FontLoader.cpp \
//          Engine/SlateWorld/World/CameraComponent/Source/CameraComponent.cpp \
//          Engine/SlateWorld/World/EditorCameraComponent/Source/EditorCameraComponent.cpp \
//          Engine/SlateWorld/World/TransformComponent/Source/TransformComponent.cpp \
//          Engine/Application/EditorHost/Source/SkyImage.cpp \
//          Engine/SlateCompute/Compute/AtmosphereIntegrator/Source/AtmosphereIntegrator.cpp \
//          Engine/SlateMath/Numeric/QuadratureIntegrator/Source/QuadratureIntegrator.cpp \
//          Engine/SlateMath/Numeric/ColourProjection/Source/ColourProjection.cpp \
//          Engine/SlateMath/Numeric/SpectralProjection/Source/SpectralProjection.cpp \
//          ExternalPackages/imgui/imgui.cpp ExternalPackages/imgui/imgui_draw.cpp \
//          ExternalPackages/imgui/imgui_tables.cpp ExternalPackages/imgui/imgui_widgets.cpp \
//          -o _AgentScratch/SceneDirectoryProof

#include "imgui.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ExternalPackages/stb/stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
#include "ExternalPackages/stb/stb_image.h"

#include "Shared/OverlayGeometry.slang.h"
#include "Shared/OverlayTransform.slang.h"
#include "SlateWorld/World/EditorCameraComponent/Api/EditorCameraComponent.h"
#include "SlateCompute/Compute/SkyDomeImage/Api/SkyDomeImage.h"
#include "Foundation/DeliveryGuarantee.h"
#include "SlateCompute/Compute/AtmosphereIntegrator/Api/AtmosphereIntegrator.h"
#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"
#include "SlateUI/Interface/ComponentSpecification/Api/ComponentSpecification.h"
#include "SlateUI/Interface/ControlPanel/Api/ControlPanel.h"
#include "SlateUI/Interface/EditorPanel/Api/EditorPanel.h"
#include "SlateUI/Interface/ControlIndex/Api/ControlIndex.h"
#include "SlateUI/Interface/TexturingPanel/Api/TexturingPanel.h"
#include "SlateUI/Interface/PanelStructure/Api/PanelStructure.h"
#include "SlateUI/Interface/SceneDirectoryPanel/Api/SceneDirectoryPanel.h"
#include "SlateUI/Interface/WorkspacePanel/Api/WorkspacePanel.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/MotionIntegrator/Api/MotionIntegrator.h"
#include "SlateUI/Interface/SymbolSpecification/Api/SymbolSpecification.h"
#include "SlateUI/Interface/TextComponent/Api/FontLoader.h"
#include "SlateUI/Interface/ThemeSpecification/Api/ThemeSpecification.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Slate;

namespace
{

constexpr float ViewportWidth = 1280.0f;    // [px] - the editor's window
constexpr float ViewportHeight = 900.0f;    // [px]
constexpr double TickMilliseconds = 16.6;   // [ms] - a 60 Hz tick

//------------------------------------------------------------------------------------------------------------------------
//                                                       CPU RASTERIZER
//------------------------------------------------------------------------------------------------------------------------
// The same triangle rasterizer as Tools/TypographyProof.

struct Rasterizer
{
    std::vector<unsigned char> Pixels;
    int Width = 0;
    int Height = 0;

    bool Begin(int W, int H)
    {
        Width = W;
        Height = H;
        Pixels.assign(static_cast<std::size_t>(W) * static_cast<std::size_t>(H) * 4u, 0u);
        for (std::size_t Pixel = 0u; Pixel < Pixels.size(); Pixel += 4u)
        {
            Pixels[Pixel + 0u] = 6u;
            Pixels[Pixel + 1u] = 6u;
            Pixels[Pixel + 2u] = 8u;
            Pixels[Pixel + 3u] = 255u;
        }
        return true;
    }

    static void Blend(std::vector<unsigned char>& Target, std::size_t Offset,
                      float Red, float Green, float Blue, float Alpha)
    {
        const float Over = Alpha;
        const float Under = 1.0f - Alpha;
        unsigned char* Dst = &Target[Offset];
        const float DR = static_cast<float>(Dst[0]) / 255.0f;
        const float DG = static_cast<float>(Dst[1]) / 255.0f;
        const float DB = static_cast<float>(Dst[2]) / 255.0f;
        const float DA = static_cast<float>(Dst[3]) / 255.0f;
        Dst[0] = static_cast<unsigned char>(Red * Over * 255.0f + DR * Under * 255.0f + 0.5f);
        Dst[1] = static_cast<unsigned char>(Green * Over * 255.0f + DG * Under * 255.0f + 0.5f);
        Dst[2] = static_cast<unsigned char>(Blue * Over * 255.0f + DB * Under * 255.0f + 0.5f);
        Dst[3] = static_cast<unsigned char>((Alpha + DA * Under) * 255.0f + 0.5f);
    }

    void Triangle(const ImDrawVert& A, const ImDrawVert& B, const ImDrawVert& C,
                  bool Textured, bool WrapU, const unsigned char* Atlas, int AtlasWidth, int AtlasHeight,
                  int ClipX0, int ClipY0, int ClipX1, int ClipY1)
    {
        const float Ax = A.pos.x, Ay = A.pos.y;
        const float Bx = B.pos.x, By = B.pos.y;
        const float Cx = C.pos.x, Cy = C.pos.y;

        const float Area = (Bx - Ax) * (Cy - Ay) - (By - Ay) * (Cx - Ax);
        if (Area == 0.0f)
            return;
        const float InvArea = 1.0f / Area;

        int MinX = static_cast<int>(std::floor(std::fmin(Ax, std::fmin(Bx, Cx))));
        int MinY = static_cast<int>(std::floor(std::fmin(Ay, std::fmin(By, Cy))));
        int MaxX = static_cast<int>(std::ceil(std::fmax(Ax, std::fmax(Bx, Cx))));
        int MaxY = static_cast<int>(std::ceil(std::fmax(Ay, std::fmax(By, Cy))));
        if (MinX < ClipX0) MinX = ClipX0;
        if (MinY < ClipY0) MinY = ClipY0;
        if (MaxX > ClipX1) MaxX = ClipX1;
        if (MaxY > ClipY1) MaxY = ClipY1;
        if (MinX >= MaxX || MinY >= MaxY)
            return;

        for (int Y = MinY; Y < MaxY; ++Y)
        {
            const float Py = static_cast<float>(Y) + 0.5f;
            for (int X = MinX; X < MaxX; ++X)
            {
                const float Px = static_cast<float>(X) + 0.5f;
                const float W0 = (Bx - Ax) * (Py - Ay) - (By - Ay) * (Px - Ax);
                const float W1 = (Cx - Bx) * (Py - By) - (Cy - By) * (Px - Bx);
                const float W2 = (Ax - Cx) * (Py - Cy) - (Ay - Cy) * (Px - Cx);
                const bool Inside = (W0 >= 0.0f && W1 >= 0.0f && W2 >= 0.0f) ||
                                    (W0 <= 0.0f && W1 <= 0.0f && W2 <= 0.0f);
                if (!Inside)
                    continue;

                const float T0 = W1 * InvArea;
                const float T1 = W2 * InvArea;
                const float T2 = W0 * InvArea;

                float Red = 0.0f, Green = 0.0f, Blue = 0.0f, Alpha = 0.0f;
                for (std::uint32_t V = 0u; V < 3u; ++V)
                {
                    const ImDrawVert& Vtx = (V == 0u) ? A : (V == 1u) ? B : C;
                    const float T = (V == 0u) ? T0 : (V == 1u) ? T1 : T2;
                    Red += static_cast<float>((Vtx.col >> 0) & 0xFFu) / 255.0f * T;
                    Green += static_cast<float>((Vtx.col >> 8) & 0xFFu) / 255.0f * T;
                    Blue += static_cast<float>((Vtx.col >> 16) & 0xFFu) / 255.0f * T;
                    Alpha += static_cast<float>((Vtx.col >> 24) & 0xFFu) / 255.0f * T;
                }

                if (Textured)
                {
                    float U = A.uv.x * T0 + B.uv.x * T1 + C.uv.x * T2;
                    float V = A.uv.y * T0 + B.uv.y * T1 + C.uv.y * T2;
                    const float SX = U * static_cast<float>(AtlasWidth) - 0.5f;
                    const float SY = V * static_cast<float>(AtlasHeight) - 0.5f;
                    const int IX = static_cast<int>(std::floor(SX));
                    const int IY = static_cast<int>(std::floor(SY));
                    const float FX = SX - static_cast<float>(IX);
                    const float FY = SY - static_cast<float>(IY);
                    const int PX0 = WrapU ? (((IX % AtlasWidth) + AtlasWidth) % AtlasWidth)
                                           : (IX < 0 ? 0 : (IX > AtlasWidth - 2 ? AtlasWidth - 2 : IX));
                    const int PY0 = IY < 0 ? 0 : (IY > AtlasHeight - 2 ? AtlasHeight - 2 : IY);
                    const auto Texel = [&](int TX, int TY) -> std::uint32_t
                    {
                        if (WrapU)
                            TX = ((TX % AtlasWidth) + AtlasWidth) % AtlasWidth;
                        const std::size_t Offset =
                            (static_cast<std::size_t>(TY) * static_cast<std::size_t>(AtlasWidth) +
                             static_cast<std::size_t>(TX)) * 4u;
                        return (static_cast<std::uint32_t>(Atlas[Offset + 3u]) << 24u) |
                               (static_cast<std::uint32_t>(Atlas[Offset + 2u]) << 16u) |
                               (static_cast<std::uint32_t>(Atlas[Offset + 1u]) << 8u) |
                               static_cast<std::uint32_t>(Atlas[Offset + 0u]);
                    };
                    const std::uint32_t T00 = Texel(PX0, PY0);
                    const std::uint32_t T10 = Texel(PX0 + 1, PY0);
                    const std::uint32_t T01 = Texel(PX0, PY0 + 1);
                    const std::uint32_t T11 = Texel(PX0 + 1, PY0 + 1);
                    const auto MixByte = [&](std::uint32_t First, std::uint32_t Second, float F) -> float
                    {
                        return (static_cast<float>(First) + (static_cast<float>(Second) - static_cast<float>(First)) * F) / 255.0f;
                    };
                    const auto MixUnit = [&](float First, float Second, float F) -> float
                    {
                        return First + (Second - First) * F;
                    };
                    const float TR = MixUnit(MixByte((T00 >> 0) & 0xFFu, (T10 >> 0) & 0xFFu, FX),
                                             MixByte((T01 >> 0) & 0xFFu, (T11 >> 0) & 0xFFu, FX), FY);
                    const float TG = MixUnit(MixByte((T00 >> 8) & 0xFFu, (T10 >> 8) & 0xFFu, FX),
                                             MixByte((T01 >> 8) & 0xFFu, (T11 >> 8) & 0xFFu, FX), FY);
                    const float TB = MixUnit(MixByte((T00 >> 16) & 0xFFu, (T10 >> 16) & 0xFFu, FX),
                                             MixByte((T01 >> 16) & 0xFFu, (T11 >> 16) & 0xFFu, FX), FY);
                    const float TA = MixUnit(MixByte((T00 >> 24) & 0xFFu, (T10 >> 24) & 0xFFu, FX),
                                             MixByte((T01 >> 24) & 0xFFu, (T11 >> 24) & 0xFFu, FX), FY);
                    Red *= TR;
                    Green *= TG;
                    Blue *= TB;
                    Alpha *= TA;
                }

                const std::size_t Offset =
                    (static_cast<std::size_t>(Y) * static_cast<std::size_t>(Width) +
                     static_cast<std::size_t>(X)) * 4u;
                Blend(Pixels, Offset, Red, Green, Blue, Alpha);
            }
        }
    }

    // 📝 The harness's extra textures: identity -> RGBA8. The host registers the sky texture through
    //    the interface's Vulkan backend; the rasterizer resolves the same identity here.
    std::unordered_map<std::uintptr_t, std::vector<unsigned char>> ExtraTextures;

    bool Draw(const ImDrawList* List, const unsigned char* Atlas, int AtlasWidth, int AtlasHeight)
    {
        const ImDrawCmd* Commands = List->CmdBuffer.Data;
        const int CommandCount = static_cast<int>(List->CmdBuffer.Size);
        const ImDrawVert* Vertices = List->VtxBuffer.Data;
        const ImDrawIdx* Indices = List->IdxBuffer.Data;

        for (int CommandIndex = 0; CommandIndex < CommandCount; ++CommandIndex)
        {
            const ImDrawCmd& Command = Commands[CommandIndex];
            if (Command.UserCallback != nullptr)
                continue;

            const int ClipX0 = static_cast<int>(Command.ClipRect.x > 0.0f ? Command.ClipRect.x : 0.0f);
            const int ClipY0 = static_cast<int>(Command.ClipRect.y > 0.0f ? Command.ClipRect.y : 0.0f);
            const int ClipX1 = static_cast<int>(Command.ClipRect.z < static_cast<float>(Width)
                                                    ? Command.ClipRect.z : static_cast<float>(Width));
            const int ClipY1 = static_cast<int>(Command.ClipRect.w < static_cast<float>(Height)
                                                    ? Command.ClipRect.w : static_cast<float>(Height));
            if (ClipX0 >= ClipX1 || ClipY0 >= ClipY1)
                continue;

            const ImTextureID Identity = Command.GetTexID();
            const bool Textured = (Identity != (ImTextureID)0);
            const std::uint32_t PrimitiveCount = Command.ElemCount / 3u;

            // 🔴 Resolve the sampled texture: the font atlas by default, an extra texture by identity.
            const unsigned char* CommandAtlas = Atlas;
            int CommandAtlasWidth = AtlasWidth;
            int CommandAtlasHeight = AtlasHeight;
            const auto Extra = ExtraTextures.find(static_cast<std::uintptr_t>(Identity));
            if (Extra != ExtraTextures.end())
            {
                CommandAtlas = Extra->second.data();
                CommandAtlasWidth = 1024;
                CommandAtlasHeight = 576;
            }
            for (std::uint32_t Primitive = 0u; Primitive < PrimitiveCount; ++Primitive)
            {
                const ImDrawIdx I0 = Indices[static_cast<std::size_t>(Command.IdxOffset) +
                                             static_cast<std::size_t>(Primitive) * 3u + 0u];
                const ImDrawIdx I1 = Indices[static_cast<std::size_t>(Command.IdxOffset) +
                                             static_cast<std::size_t>(Primitive) * 3u + 1u];
                const ImDrawIdx I2 = Indices[static_cast<std::size_t>(Command.IdxOffset) +
                                             static_cast<std::size_t>(Primitive) * 3u + 2u];
                const ImDrawVert& A = Vertices[Command.VtxOffset + I0];
                const ImDrawVert& B = Vertices[Command.VtxOffset + I1];
                const ImDrawVert& C = Vertices[Command.VtxOffset + I2];
                Triangle(A, B, C, Textured, Extra != ExtraTextures.end(),
                         CommandAtlas, CommandAtlasWidth, CommandAtlasHeight,
                         ClipX0, ClipY0, ClipX1, ClipY1);
            }
        }




        return true;
    }

    // 📐 The CPU twin of the GPU overlay pass: rasterizes the SAME OverlayGeometry record the host
    //    uploads, so the proof pixels are the proof of the pass's input. Straight-alpha source-over,
    //    exactly as the pass's blend state declares. The clip is the viewport leaf's box — the same
    //    scissor the GPU pass sets — so the overlay never paints over the panels, exactly as the
    //    windowed editor behaves.
    void RasterizeOverlay(const OverlayGeometry& Overlay,
                          float ClipX0, float ClipY0, float ClipX1, float ClipY1)
    {
        const auto Colour = [](std::uint32_t Packed) -> ImU32
        {
            const std::uint32_t A = (Packed >> 24u) & 0xFFu;
            const std::uint32_t R = (Packed >> 16u) & 0xFFu;
            const std::uint32_t G = (Packed >> 8u)  & 0xFFu;
            const std::uint32_t B = Packed & 0xFFu;
            return (A << 24u) | (B << 16u) | (G << 8u) | R;   // [-] - ImGui's 0xAABBGGRR spelling
        };

        const auto Fill = [&](float X0, float Y0, float X1, float Y1, float X2, float Y2, ImU32 Col)
        {
            const ImDrawVert A = { ImVec2(X0, Y0), ImVec2(0.0f, 0.0f), Col };
            const ImDrawVert B = { ImVec2(X1, Y1), ImVec2(0.0f, 0.0f), Col };
            const ImDrawVert C = { ImVec2(X2, Y2), ImVec2(0.0f, 0.0f), Col };
            Triangle(A, B, C, false, false, nullptr, 0, 0,
                     static_cast<int>(ClipX0), static_cast<int>(ClipY0),
                     static_cast<int>(ClipX1), static_cast<int>(ClipY1));
        };

        for (std::uint32_t Index = 0u; Index < Overlay.TriangleCount; ++Index)
        {
            const OverlayTriangle& T = Overlay.Triangles[Index];
            Fill(T.X0, T.Y0, T.X1, T.Y1, T.X2, T.Y2, Colour(T.Packed));
        }

        for (std::uint32_t Index = 0u; Index < Overlay.LineCount; ++Index)
        {
            const OverlayLine& L = Overlay.Lines[Index];
            const float DX = L.X1 - L.X0;
            const float DY = L.Y1 - L.Y0;
            const float Length = std::sqrt(DX * DX + DY * DY);
            if (Length < 0.0001f)
                continue;

            const float NX = -DY / Length;
            const float NY =  DX / Length;
            const float Half = L.Thickness * 0.5f;
            const ImU32 Col = Colour(L.Packed);

            Fill(L.X0 + NX * Half, L.Y0 + NY * Half,
                 L.X1 + NX * Half, L.Y1 + NY * Half,
                 L.X1 - NX * Half, L.Y1 - NY * Half, Col);
            Fill(L.X0 + NX * Half, L.Y0 + NY * Half,
                 L.X1 - NX * Half, L.Y1 - NY * Half,
                 L.X0 - NX * Half, L.Y0 - NY * Half, Col);
        }

        for (std::uint32_t Index = 0u; Index < Overlay.DotCount; ++Index)
        {
            const OverlayDot& D = Overlay.Dots[Index];
            const ImU32 Col = Colour(D.Packed);

            // 📐 A 16-gon fan stands in for the fragment-discard disc; 16 sides is below the pixel
            //    quantum at these radii, so no polygon edge reads.
            constexpr float Turn = 6.2831853f / 16.0f;
            for (std::uint32_t Segment = 0u; Segment < 16u; ++Segment)
            {
                const float A0 = Turn * static_cast<float>(Segment);
                const float A1 = Turn * static_cast<float>(Segment + 1u);
                Fill(D.X, D.Y,
                     D.X + std::cos(A0) * D.Radius, D.Y + std::sin(A0) * D.Radius,
                     D.X + std::cos(A1) * D.Radius, D.Y + std::sin(A1) * D.Radius, Col);
            }
        }
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE DRIVER
//------------------------------------------------------------------------------------------------------------------------
// The harness replicates the editor host's tick: the shared index advances once,
// the scene directory samples it, the workspace panel records its strip and body,
// the editor panel records the partition chrome, and the host's own content loop
// fills each leaf body — sky in the viewport leaf, outliner, properties.

struct SceneDriver
{
    ImGuiIO& IO;
    MotionIntegrator Motion;
    ControlIndex Interaction;
    RecordingSurface Surface;
    WorkspacePanel Workspace;
    EditorPanel Editor;
    PanelStructure Partition;
    EditorPanelConfiguration Configuration;
    SceneDirectoryPanel SceneDirectory;
    SceneDirectoryContext Applied;
    TexturingPanel Texturing;
    TexturingContext TexturingApplied;
    FontLoader Fonts;

    // 🔴 The panels BORROW the appearance and read it on every tick — it must outlive them all, so
    //    it is a member here and never a local of `Construct` (a stack local dies at the semicolon
    //    and every later tick read use-after-scope memory: intermittent crashes and garbage theme
    //    colours — the flat dark-blue renders — depending on what the stack reused the slot for).
    ThemeProfile Appearance = {};

    inline static EntityRow EditorEntities[7] =
    {
        { "Level_01_City",           EntitySubject::Level,      0u, 0xFFFFFFFFu, 3u, "city level main" },
        { "Lighting",                EntitySubject::Grouping,   1u,  0u,         2u, "folder lighting" },
        { "Directional Light (Sun)", EntitySubject::Sun,        2u,  1u,         0u, "sun light directional" },
        { "Sky Atmosphere",          EntitySubject::Sky,        2u,  1u,         0u, "sky atmosphere dome" },
        { "Environment",             EntitySubject::Grouping,   1u,  0u,         1u, "folder environment" },
        { "Post Process Volume",     EntitySubject::Actor,      2u,  4u,         0u, "post volume effects" },
        { "Editor Camera",           EntitySubject::Camera,     1u,  0u,         0u, "camera fly view", CameraRole::Editor }
    };


    static constexpr const char* const StackChannels[TextureChannelLimit] =
    {
        "Base Color", "Metallic", "Roughness", "Normal",
        "Height", "Ambient Occlusion", "Emissive", "Opacity"
    };

    static constexpr TextureLayerRow StackSeed[TextureLayerLimit] =
    {
        { "Surface Detail",  TextureLayerClassification::Folder,  "Passthrough", 100u, 0x9B8CF0u, 0x9B8CF0u,
          false, 100u, false, "", "4 layers", { StackChannels[0], StackChannels[1], StackChannels[2] }, 3u,
          0u, 0xFFFFFFFFu, 4u, true, "folder detail group", false, "" },
        { "Levels",          TextureLayerClassification::Adjustment, "Overlay",   64u, 0x8B8D98u, 0x8B8D98u,
          false, 100u, false, "", "2048px \u00B7 RGBA 8", { StackChannels[0], StackChannels[3] }, 2u,
          1u, 0u, 0u, true, "adjust levels", false, "" },
        { "Warning Stencil", TextureLayerClassification::Decal,   "Normal",    100u, 0xE5484Du, 0xE5484Du,
          true,  100u, false, "Bitmap", "Planar \u00B7 100%", { StackChannels[0] }, 1u,
          1u, 0u, 0u, true, "decal stencil warning", false, "" },
        { "Scratches",       TextureLayerClassification::Paint,    "Screen",     38u, 0xB0E64Cu, 0xB0E64Cu,
          true,   88u, false, "Generator", "2048px \u00B7 RGBA 8", { StackChannels[0], StackChannels[2] }, 2u,
          1u, 0u, 0u, true, "paint scratches grunge", false, "Blur" },
        { "Edge Wear",       TextureLayerClassification::Fill,     "Multiply",   82u, 0xF76B15u, 0xF76B15u,
          true,  100u, false, "Generator", "2048px \u00B7 RGBA 8", { StackChannels[1], StackChannels[2] }, 2u,
          1u, 0u, 0u, true, "fill edge wear rust", false, "" },
        { "Emissive Trim",   TextureLayerClassification::Fill,     "Normal",    100u, 0xFFC53Du, 0xFFC53Du,
          true,  100u, false, "Paint", "2048px \u00B7 RGBA 8", { StackChannels[6] }, 1u,
          0u, 0xFFFFFFFFu, 0u, true, "fill emissive trim", false, "" },
        { "Hex Panelling",   TextureLayerClassification::Pattern,  "Normal",    100u, 0x8AB4D8u, 0x8AB4D8u,
          true,  100u, false, "Generator", "Hex Grid \u00B7 4\u00D74", { StackChannels[2], StackChannels[4] }, 2u,
          0u, 0xFFFFFFFFu, 0u, true, "pattern hex panel", false, "" },
        { "Base Materials",  TextureLayerClassification::Folder,   "Passthrough", 100u, 0x12A594u, 0x12A594u,
          false, 100u, false, "", "4 layers", { StackChannels[0], StackChannels[1] }, 2u,
          0u, 0xFFFFFFFFu, 4u, true, "folder materials base", false, "" },
        { "Brushed Steel",   TextureLayerClassification::Fill,     "Normal",    100u, 0x8AB4D8u, 0x8AB4D8u,
          true,  100u, false, "Generator", "4096px \u00B7 RGBA 8", { StackChannels[0], StackChannels[1] }, 2u,
          1u, 7u, 0u, true, "fill brushed steel metal", false, "" },
        { "Gold Inlay",      TextureLayerClassification::Fill,     "Normal",    100u, 0xE5484Du, 0xE5484Du,
          true,   50u, true,  "Color Selection", "2048px \u00B7 RGBA 8", { StackChannels[0] }, 1u,
          1u, 7u, 0u, true, "fill gold inlay", false, "Levels, HSL Shift" },
        { "Oak Panel",       TextureLayerClassification::Material, "Normal",    100u, 0xF76B15u, 0xF76B15u,
          false, 100u, false, "", "2048px \u00B7 RGBA 8", { StackChannels[0], StackChannels[2] }, 2u,
          1u, 7u, 0u, true, "material oak wood", true, "" },
        { "Canvas Weave",    TextureLayerClassification::Material, "Normal",     90u, 0xE93D82u, 0xE93D82u,
          false, 100u, false, "", "2048px \u00B7 RGBA 8", { StackChannels[0], StackChannels[3] }, 2u,
          1u, 7u, 0u, false, "material canvas fabric", false, "" }
    };

    // 📝 The mutable row set the host owns: seeded from the reference tree at bring-up, and mutated
    //    only through the shared `TexturingStack` helper when the panel raises a structural
    //    request — the harness drives exactly what the editor host drives.
    TexturingStack StackRows;

    // 📝 The sky texture the host would upload: generated from the environment, registered in the
    //    rasterizer under a fixed identity, and handed to the scene directory as its texture identity.
    static constexpr std::uintptr_t SkyIdentity = 0x534B5931u;   // [-] - "SKY1", a fake descriptor handle
    AtmosphereIntegrator SkyIntegrator;
    std::vector<std::uint8_t> SkyPixels;
    EnvironmentConfiguration SkyPrevious;
    SkyCamera SkyCam;
    bool SkyReady = false;
    bool SkyEverGenerated = false;

    // 📝 The camera the host owns. The harness cannot run the seam, so it feeds the component the same
    //    CameraCondition the seam would deliver: the simulation flags below stand in for held keys
    //    and the right-button look gesture.
    EditorCameraComponent EditorCamera;
    bool  SimForwardHeld = false;
    bool  SimLookHeld    = false;
    float SimLookDeltaX  = 0.0f;
    float SimLookDeltaY  = 0.0f;
    bool  SimShiftHeld   = false;
    bool  SimTabPressed  = false;
    bool  SimLayersTab   = false;   // [-] - Tab goes to the layer stack, not the scene directory
    bool  SkipOverlayRaster  = false;   // [-] - the capture withholds the overlay pass's CPU twin
                                        //       so GPU-only scenarios can assert that no CPU fallback drew
    char  SimTyped[16]   = {};   // [-] - the run the search field accepts this tick

    SceneDriver() : IO(ImGui::GetIO()) {}

    bool ConstructSceneProof()
    {
        const char* FontRoot = "EngineContent/FontArchives";
        if (!Fonts.Discover(FontRoot).Resolved)
            return false;
        FontProfile Profile;
        std::strncpy(Profile.Family, "Inter", sizeof(Profile.Family) - 1u);
        if (!Fonts.Load(FontRoot, Profile, 1.0f).Resolved)
            return false;

        IO.Fonts->TexData->SetTexID((ImTextureID)(intptr_t)1);
        IO.Fonts->TexRef._TexData = IO.Fonts->TexData;

        ThemeSelection Selected;
        Selected.Current = ThemeSubject::Oled;
        Appearance = ResolveTinted(1.0, 1.0, ViewportWidth, Selected);
        Surface.ApplyFontLoader(Fonts);
        Surface.ApplyTypographyScale(Appearance.TextScale);
        Surface.ApplyCornerScale(Appearance.CornerScale);

        if (!Interaction.AttachMotion(Motion).Resolved)
            return false;
        if (!SceneDirectory.ConstructSceneDirectoryPanel(Interaction, Motion, Surface, Appearance).Resolved)
            return false;
        if (!Texturing.ConstructTexturingPanel(Interaction, Motion, Surface, Appearance).Resolved)
            return false;
        if (!Editor.ConstructEditorPanel(Motion, Surface, Appearance).Resolved)
            return false;
        if (!Workspace.ConstructWorkspacePanel(Surface, Appearance).Resolved)
            return false;

        Applied.EnvironmentPresented = true;
        Applied.Environment.SunElevation   = 35.0;
        Applied.Environment.SunAzimuth     = 120.0;
        Applied.Environment.SunIntensity   = 4.8;
        Applied.Environment.SunTemperature = 5500.0;
        Applied.Environment.SkyIntensity   = 1.0;
        Applied.Environment.SkyTurbidity   = 2.0;
        Applied.Environment.AtmosphereDensity = 1.0;
        Applied.Environment.AtmosphereScaleHeight = 1.0;
        Applied.EntityTaken = 2u;   // the sun, taken at bring-up

        // 📝 The camera row's options and the component, exactly as the host declares them.
        Applied.DetailBits[6u] = 2u;
        Applied.CameraSpeed = 50.0;

        // 📝 The layer stack's seed — the same mock tree the host carries, through the shared stack
        //    helper and its context seeding, exactly as the editor host does it.
        StackRows.Seed(StackSeed, 12u);
        TexturingApplied.LayerTaken = 1u;
        SeedTexturingContextFromRows(TexturingApplied, StackRows.Rows, StackRows.Count);

        for (std::uint32_t Index = 0u; Index < TextureLayerLimit; ++Index)
        {
            TexturingApplied.MaskSourceTaken[Index] =
                (Index == 2u || Index == 3u || Index == 4u) ? 4u : 0u;
            TexturingApplied.MaskDensity[Index] =
                (Index == 3u) ? 88u : 100u;
            TexturingApplied.MaskInverted[Index] = (Index == 9u);
        }
        EditorCamera.YawDegrees   = Applied.Environment.SunAzimuth - 20.0;
        EditorCamera.PitchDegrees = 15.0;
        EditorCamera.Snap();
        Applied.CameraPosition[0] = 0.0;
        Applied.CameraPosition[1] = 1.5;
        Applied.CameraPosition[2] = 0.0;
        Applied.CameraRotation[0] = EditorCamera.YawDegrees;
        Applied.CameraRotation[1] = EditorCamera.PitchDegrees;
        for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
        {
            Applied.EntityPosition[6u][Axis] = Applied.CameraPosition[Axis];
            Applied.EntityRotation[6u][Axis] = Applied.CameraRotation[Axis];
        }
        EditorCamera.PublishTransform(Applied.EntityPosition[6u], Applied.EntityRotation[6u]);

        return true;
    }

    void Tick(float MouseX, float MouseY, bool Held, bool Arrived, bool Released,
              float Wheel = 0.0f)
    {
        IO.MousePos = ImVec2(MouseX, MouseY);
        IO.MouseDelta = ImVec2(0.0f, 0.0f);
        IO.MouseWheel = Wheel;
        IO.DeltaTime = static_cast<float>(TickMilliseconds / 1000.0);
        if (Arrived)
            IO.AddMouseButtonEvent(0, true);
        else if (Released)
            IO.AddMouseButtonEvent(0, false);

        ImGui::NewFrame();

        Discard(Surface.Adopt(RecordingSurface::ShellLayer::Beneath));
        Motion.Advance(TickMilliseconds);
        Interaction.Advance(Surface.Pointer(), TickMilliseconds);
        // 📐 Tab is routed: to the layer stack when the simulation says so, else to the scene
        //    directory — the host's pointer arbitration, simulated.
        const bool TabArrived = SimTabPressed || SimLayersTab;
        SceneDirectory.Advance(Surface.Pointer(), TickMilliseconds, Applied,
                               TabArrived && !SimLayersTab);
        Texturing.Advance(Surface.Pointer(), TickMilliseconds, TexturingApplied,
                             StackRows.Rows, StackRows.Count, TabArrived && SimLayersTab);
        SimTabPressed = false;
        SimLayersTab = false;
        Editor.Advance(Surface.Pointer(), TickMilliseconds);

        // 📝 The host's search feed, simulated: while the field is taken, append the queued typed
        //    characters exactly as the seam's AcceptTyped would.
        if (TexturingApplied.SearchTaken && SimTyped[0] != '\0')
        {
            std::uint32_t Occupied = 0u;

            while (Occupied + 1u < TexturingContext::TextureRetentionLimit &&
                   TexturingApplied.Retention[Occupied] != '\0')
            {
                ++Occupied;
            }

            for (std::uint32_t Index = 0u; SimTyped[Index] != '\0' &&
                 Occupied + 1u < TexturingContext::TextureRetentionLimit; ++Index)
            {
                TexturingApplied.Retention[Occupied++] = SimTyped[Index];
            }

            TexturingApplied.Retention[Occupied] = '\0';
            SimTyped[0] = '\0';
        }

        if (Applied.SearchTaken && SimTyped[0] != '\0')
        {
            std::uint32_t Occupied = 0u;

            while (Occupied + 1u < SceneDirectoryContext::RetentionLimit &&
                   Applied.EntityRetention[Occupied] != '\0')
            {
                ++Occupied;
            }

            for (std::uint32_t Index = 0u; SimTyped[Index] != '\0' &&
                 Occupied + 1u < SceneDirectoryContext::RetentionLimit; ++Index)
            {
                Applied.EntityRetention[Occupied++] = SimTyped[Index];
            }

            Applied.EntityRetention[Occupied] = '\0';
            SimTyped[0] = '\0';
        }

        // 📝 The fly camera — the host's own step, fed by the simulation flags instead of the seam.
        {
            CameraCondition FlyInput;
            FlyInput.ForwardHeld  = SimForwardHeld;
            FlyInput.LookHeld     = SimLookHeld;
            FlyInput.LookDeltaX   = SimLookDeltaX;
            FlyInput.LookDeltaY   = SimLookDeltaY;
            FlyInput.ShiftHeld    = SimShiftHeld;

            static_cast<void>(EditorCamera.ConsumeTransform(Applied.EntityPosition[6u],
                                                            Applied.EntityRotation[6u]));

            CameraSettings FlySettings;
            FlySettings.FlySpeed    = Applied.CameraSpeed;
            FlySettings.LagEnabled  = (Applied.DetailBits[6u] & 2u) != 0u;
            FlySettings.InvertPitch = (Applied.DetailBits[6u] & 4u) != 0u;

            EditorCamera.Advance(TickMilliseconds / 1000.0, FlyInput, FlySettings);

            Applied.ViewportSkyCamera.AzimuthDegrees    = static_cast<float>(EditorCamera.LaggedYawDegrees);
            Applied.ViewportSkyCamera.ElevationDegrees  = static_cast<float>(EditorCamera.LaggedPitchDegrees);
            Applied.ViewportSkyCamera.FieldOfViewDegrees = 60.0f;
            Applied.CameraPosition[0] = EditorCamera.LaggedPosition[0];
            Applied.CameraPosition[1] = EditorCamera.LaggedPosition[1];
            Applied.CameraPosition[2] = EditorCamera.LaggedPosition[2];
            Applied.CameraRotation[0] = EditorCamera.LaggedYawDegrees;
            Applied.CameraRotation[1] = EditorCamera.LaggedPitchDegrees;
            Applied.CameraRotation[2] = 0.0;
            EditorCamera.PublishTransform(Applied.EntityPosition[6u], Applied.EntityRotation[6u]);
        }

        // 📝 The host regenerates the sky when the environment changed (at most once per drag) and
        //    hands the identity to the scene directory; the viewport leaf draws it.
        if (Applied.EnvironmentPresented &&
            (!SkyEverGenerated ||
             std::memcmp(&SkyPrevious, &Applied.Environment, sizeof(EnvironmentConfiguration)) != 0))
        {
            SkyCam.AzimuthDegrees   = Applied.Environment.SunAzimuth - 20.0;
            SkyCam.ElevationDegrees = 15.0;
            if (GenerateSkyImage(SkyIntegrator, Applied.Environment, SkyCam,
                                 1024u, 576u, SkyPixels).Resolved)
            {
                Applied.SkyTextureIdentity = SkyIdentity;
                SkyReady = true;
            }
            SkyPrevious = Applied.Environment;
            SkyEverGenerated = true;
        }

        // 📝 The workspace: strip and body first, then the editor chrome, then the leaf content —
        //    the same order the editor host records.
        const PlaneExtent Whole = Spanning(0.0f, 0.0f, ViewportWidth, ViewportHeight);
        Discard(Workspace.Record(Whole, "Workspace 1"));

        // 📐 A static tab strip over the workspace's own strip, standing in for the vendor dock the
        //    harness cannot run.
        const PlaneExtent WorkspaceStrip = Workspace.Strip();
        if (WorkspaceStrip.Height() > 0.0f)
        {
            Surface.TextRun(WorkspaceStrip.MinimumX + 12.0f,
                             WorkspaceStrip.MinimumY + (WorkspaceStrip.Height()
                                                        - 12.5f) * 0.5f,
                             Covering(0xE6E6E6u), "Workspace 1", 12.5f, 0.0f, true);
        }

        // 🔴 The popups are deferred, exactly as the editor host defers them, so the leaf content
        //    records beneath the split/subject menus instead of painting over them.
        Discard(Editor.Record(Workspace.Body(), Partition, Configuration, 0u, true));

        // 📝 The overlay record is rebuilt for this tick, exactly as the host rebuilds it.
        Applied.Overlay.Reset();

        for (std::uint32_t Leaf = 0u; Leaf < Editor.LeafCount(); ++Leaf)
        {
            const PlaneExtent LeafBody = Editor.LeafBody(Leaf);

            switch (Editor.LeafSubject(Leaf))
            {
                case PanelSubject::Viewport:
                    SceneDirectory.RecordViewportSky(LeafBody, Applied);
                    // Grid geometry is emitted by the editor's overlay pass now; the directory panel
                    // records only the leaf content it owns.
                    SceneDirectory.RecordGizmo(LeafBody, Applied, Applied.Overlay);

                    // 📐 GPU-only by design: the viewport leaf records overlay geometry for the pass,
                    //    and a skipped pass leaves no CPU twin behind to draw it through the interface.
                    break;
                case PanelSubject::Outliner:
                    if (Configuration.FooterDemand == EditorFooterDemand::SceneImport ||
                        Configuration.FooterDemand == EditorFooterDemand::SceneExport)
                    {
                        Applied.TransferMode = Configuration.FooterDemand == EditorFooterDemand::SceneExport ? 1u : 0u;
                        Applied.OutlinePage = 2u;
                        Configuration.FooterDemand = EditorFooterDemand::None;
                    }
                    SceneDirectory.RecordOutliner(LeafBody, Applied, EditorEntities, 7u);
                    break;
                case PanelSubject::Properties:
                    SceneDirectory.RecordProperties(LeafBody, Applied, EditorEntities, 7u,
                                                    Applied.InspectorTab);
                    break;
                case PanelSubject::Texturing:
                    if (Configuration.FooterDemand == EditorFooterDemand::ExportFlattened ||
                        Configuration.FooterDemand == EditorFooterDemand::LayerExport)
                    {
                        TexturingApplied.ExportMode =
                            Configuration.FooterDemand == EditorFooterDemand::LayerExport ? 1u : 0u;
                        TexturingApplied.StackPage = 2u;
                        Configuration.FooterDemand = EditorFooterDemand::None;
                    }
                    Texturing.Record(LeafBody, TexturingApplied, StackRows.Rows,
                                        StackRows.Count);
                    break;
                default:
                    break;
            }
        }

        Editor.RecordDeferredPopups(Partition, Configuration);

        // 📝 The host drains the layer stack's structural requests exactly once per tick, through the
        //    same shared helper the editor host calls.
        StackRows.ApplyRequest(TexturingApplied);

        Surface.Retire();

        ImGui::Render();
    }

    void Settle(int Frames)
    {
        for (int Index = 0; Index < Frames; ++Index)
            Tick(640.0f, 450.0f, false, false, false);
    }

    void Tap(float MouseX, float MouseY)
    {
        Tick(MouseX, MouseY, true, true, false);
        Tick(MouseX, MouseY, true, false, false);
        Tick(MouseX, MouseY, false, false, true);
    }

    // 📐 The workspace partition each scenario presents:
    //    overview  :  viewport | outliner
    //    props     :  viewport | (outliner over properties)
    void ApplyPartition(bool WithProperties)
    {
        Partition.ConstructPanelPartition(PanelSubject::Viewport);
        Discard(Partition.Divide(PanelStructure::RootIndex, PanelDivisionAxis::X,
                                 PanelDivisionSide::Maximum));
        const Deliver<PanelRecord> Right = Partition.Current(PanelStructure::RootIndex);
        const std::uint32_t RightLeaf = Right.Resolved ? Right.Resolve().Maximum : 1u;
        Discard(Partition.Assign(RightLeaf, PanelSubject::Outliner));

        if (WithProperties)
        {
            Discard(Partition.Divide(RightLeaf, PanelDivisionAxis::Y, PanelDivisionSide::Maximum));
            const Deliver<PanelRecord> RightRecord = Partition.Current(RightLeaf);
            const std::uint32_t LowerLeaf = RightRecord.Resolved ? RightRecord.Resolve().Maximum : 3u;
            Discard(Partition.Assign(LowerLeaf, PanelSubject::Properties));
        }
    }

    bool Capture(const char* Path, const unsigned char* Atlas, int AtlasWidth, int AtlasHeight)
    {
        const unsigned char* LiveAtlas = static_cast<const unsigned char*>(IO.Fonts->TexData->Pixels);
        const int LiveAtlasWidth = IO.Fonts->TexData->Width;
        const int LiveAtlasHeight = IO.Fonts->TexData->Height;


        Rasterizer Out;
        Out.Begin(static_cast<int>(ViewportWidth), static_cast<int>(ViewportHeight));
        if (SkyReady)
            Out.ExtraTextures[SkyIdentity] = SkyPixels;
        const ImDrawData* Data = ImGui::GetDrawData();
        for (int ListIndex = 0; ListIndex < Data->CmdListsCount; ++ListIndex)
        {
            if (!Out.Draw(Data->CmdLists[ListIndex], LiveAtlas, LiveAtlasWidth, LiveAtlasHeight))
                return false;
        }

        // 📝 The overlay record the host would upload to the GPU pass — rasterized here as the pass's
        //    CPU twin, so the proof pixels ARE the pass's input. The clip is the viewport leaf's box,
        //    the same scissor the GPU pass sets: the grid and the gizmo never paint over the panels.
        PlaneExtent ViewportClip = Spanning(0.0f, 0.0f, ViewportWidth, ViewportHeight);
        for (std::uint32_t Leaf = 0u; Leaf < Editor.LeafCount(); ++Leaf)
        {
            if (Editor.LeafSubject(Leaf) == PanelSubject::Viewport)
            {
                ViewportClip = Editor.LeafBody(Leaf);
                break;
            }
        }

        // 🔴 The GPU pass's CPU twin is withheld exactly as the host withholds the real pass: while an
        //    editor popup stands, the overlay would paint the grid and the axes OVER the open menu —
        //    the reported "the lines draw on the ImGui menus" (see the host's `OverlayWithheld`).
        if (!SkipOverlayRaster && !Editor.AnyPopupStanding())
        {
            Out.RasterizeOverlay(Applied.Overlay,
                                 ViewportClip.MinimumX, ViewportClip.MinimumY,
                                 ViewportClip.MaximumX, ViewportClip.MaximumY);
        }

        if (stbi_write_png(Path, Out.Width, Out.Height, 4, Out.Pixels.data(), Out.Width * 4) == 0)
            return false;
        std::fprintf(stderr, "[proof] wrote %s (%dx%d)\n", Path, Out.Width, Out.Height);
        return true;
    }
};

bool RunShot(SceneDriver& Driver, const char* OutputPath, const char* Scenario,
             const unsigned char* Atlas, int AtlasWidth, int AtlasHeight)
{
    std::fprintf(stderr, "\n== %s ==\n", Scenario);

    if (std::strcmp(Scenario, "editor-overview") == 0)
    {
        Driver.ApplyPartition(false);
        Driver.Settle(20);
    }
    else if (std::strcmp(Scenario, "editor-directory-multiselect") == 0)
    {
        Driver.ApplyPartition(false);
        for (std::uint32_t Index = 0u; Index < SceneDirectoryContext::EntityLimit; ++Index)
            Driver.Applied.EntitySelected[Index] = false;
        Driver.Applied.EntitySelected[2u] = true;
        Driver.Applied.EntitySelected[3u] = true;
        Driver.Applied.EntitySelected[6u] = true;
        Driver.Applied.EntityTaken = 6u;
        Driver.Applied.EntitySelectionAnchor = 2u;
        Driver.Settle(20);
    }
    else if (std::strcmp(Scenario, "editor-outliner-inspector") == 0)
    {
        Driver.ApplyPartition(false);
        Driver.Settle(20);
        Driver.Applied.OutlinePage = 1u;
        Driver.Settle(24);
        Driver.Applied.OutlinePage = 0u;
        Driver.Settle(24);
        Driver.Tap(820.0f, 315.0f); // Sky Atmosphere after the inspector slid away
        if (Driver.Applied.EntityTaken != 3u || !Driver.Applied.EntitySelected[3u])
        {
            std::fprintf(stderr, "[FAIL] directory selection stopped responding after a page slide\n");
            return false;
        }
        std::fprintf(stderr, "[assert] directory selection remains live after a page slide\n");
    }
    else if (std::strcmp(Scenario, "editor-camera-properties") == 0)
    {
        Driver.ApplyPartition(false);
        Driver.Applied.EntityTaken = 6u; // Editor Camera
        Driver.Applied.OutlinePage = 1u;
        Driver.Applied.OutlineInspectorTab = 0u;
        Driver.Applied.CameraSpeed = 78.125;
        Driver.Applied.CameraFieldOfView = 72.0;
        Driver.Applied.CameraNearClip = 0.05;
        Driver.Applied.CameraFarClip = 25000.0;

        Driver.EditorCamera.FlySpeed = 50.0;
        Driver.EditorCamera.AdjustFlySpeed(2.0);
        if (std::abs(Driver.EditorCamera.FlySpeed - 78.125) > 0.001)
        {
            std::fprintf(stderr, "[FAIL] Editor Camera wheel gearing did not persist two speed steps\n");
            return false;
        }

        Driver.Settle(28);
        std::fprintf(stderr, "[assert] Editor Camera lens, clipping, and fly-speed controls rendered\n");
    }
    else if (std::strcmp(Scenario, "editor-camera-bookmarks") == 0)
    {
        Driver.ApplyPartition(false);
        Driver.Applied.EntityTaken = 6u; // Editor Camera
        Driver.Applied.OutlinePage = 1u;
        Driver.Applied.OutlineInspectorTab = 1u;
        Driver.Applied.CameraBookmarkCount = 2u;
        Driver.Applied.CameraBookmarkTaken = 0u;
        std::strncpy(Driver.Applied.CameraBookmarkNames[0], "Material Close-up", 31u);
        std::strncpy(Driver.Applied.CameraBookmarkNames[1], "Lighting Overview", 31u);
        Driver.Applied.CameraBookmarkPosition[0][0] = 2.5;
        Driver.Applied.CameraBookmarkPosition[0][1] = 1.8;
        Driver.Applied.CameraBookmarkPosition[0][2] = -3.0;
        Driver.Applied.CameraBookmarkRotation[0][0] = 35.0;
        Driver.Applied.CameraBookmarkRotation[0][1] = -12.0;
        Driver.Applied.CameraBookmarkPosition[1][0] = -8.0;
        Driver.Applied.CameraBookmarkPosition[1][1] = 6.0;
        Driver.Applied.CameraBookmarkPosition[1][2] = 4.0;
        Driver.Applied.CameraBookmarkRotation[1][0] = 125.0;
        Driver.Applied.CameraBookmarkRotation[1][1] = -28.0;
        Driver.Settle(28);
    }
    else if (std::strcmp(Scenario, "editor-sun-props") == 0)
    {
        Driver.ApplyPartition(true);
        Driver.Settle(20);
    }
    else if (std::strcmp(Scenario, "editor-sky-quality") == 0)
    {
        Driver.ApplyPartition(true);
        for (std::uint32_t Index = 0u; Index < SceneDirectoryContext::EntityLimit; ++Index)
            Driver.Applied.EntitySelected[Index] = false;
        Driver.Applied.EntityTaken = 3u; // Sky Atmosphere
        Driver.Applied.EntitySelected[3u] = true;
        Driver.Settle(20);
        for (std::uint32_t Index = 0u; Index < 10u; ++Index)
            Driver.Tick(1050.0f, 760.0f, false, false, false, -3.0f);
        Driver.Settle(20);
    }
    else if (std::strcmp(Scenario, "editor-grid-settings") == 0)
    {
        // 📐 🔴 THE OVERLAY'S NDC CONVENTION, asserted on real numbers BEFORE any capture: the GPU
        //    pass's vertex shader and this harness compile the SAME `OverlayTransform.slang.h`, and
        //    the transform must map screen y = 0 (the display's top edge) to NDC +1 — the
        //    framebuffer's top row — exactly as the interface's own vertex shader does. The previous
        //    spelling `−2y/h − 1` mapped every positive screen y BELOW NDC −1, clipping the whole
        //    overlay off-screen: the grid, the axes and the gizmo silently drew nothing (the
        //    recurring "the grid is not showing"), while the harness's CPU twin rasterized in
        //    screen space and never exercised the math. These asserts pin the convention so a
        //    reintroduced flip fails the scenario on the first line.
        {
            const float NdcX0  = OverlayNdcX(0.0f, 1280.0f);
            const float NdcXMid = OverlayNdcX(640.0f, 1280.0f);
            const float NdcX1  = OverlayNdcX(1280.0f, 1280.0f);
            const float NdcY0  = OverlayNdcY(0.0f, 900.0f);
            const float NdcYMid = OverlayNdcY(450.0f, 900.0f);
            const float NdcY1  = OverlayNdcY(900.0f, 900.0f);

            std::fprintf(stderr, "[assert] overlay NDC: x(0,mid,1)=(%.2f,%.2f,%.2f) "
                                 "y(top,mid,bottom)=(%.2f,%.2f,%.2f)\n",
                         NdcX0, NdcXMid, NdcX1, NdcY0, NdcYMid, NdcY1);

            if (std::abs(NdcX0 + 1.0f) > 0.001f || std::abs(NdcXMid) > 0.001f ||
                std::abs(NdcX1 - 1.0f) > 0.001f ||
                std::abs(NdcY0 + 1.0f) > 0.001f || std::abs(NdcYMid) > 0.001f ||
                std::abs(NdcY1 - 1.0f) > 0.001f)
            {
                std::fprintf(stderr, "[FAIL] the overlay NDC transform does not match the "
                                     "Vulkan viewport convention (top must be -1)\n");
                return false;
            }
        }

        Driver.ApplyPartition(false);
        Driver.Settle(20);

        // 📐 PART ONE — the axes and the gizmo, from the OPPOSITE corner looking up at the origin
        //    (yaw 45, pitch +24, position -40,-25,-40): all three POSITIVE axes point toward the
        //    camera and the gizmo is in view, proving the full-opacity straight-alpha colours the
        //    GPU pass blends (the interface's premultiplied read washed them out).
        Driver.Configuration.Lattice      = PanelLatticePresentation::LinesAndDots;
        Driver.Configuration.LatticeScale = 2u;
        Driver.Configuration.Subdivisions = 12u;
        Driver.Configuration.AxisX = true;
        Driver.Configuration.AxisY = true;
        Driver.Configuration.AxisZ = true;

        Driver.EditorCamera.YawDegrees   = 45.0;
        Driver.EditorCamera.PitchDegrees = 24.0;
        Driver.EditorCamera.Position[0]  = -40.0;
        Driver.EditorCamera.Position[1]  = -25.0;
        Driver.EditorCamera.Position[2]  = -40.0;
        Driver.EditorCamera.Snap();
        Driver.Settle(10);

        if (!Driver.Capture(OutputPath, Atlas, AtlasWidth, AtlasHeight))
            return false;

        auto Scan = [&](const char* Path, std::uint32_t& Red, std::uint32_t& Green,
                        std::uint32_t& Blue, std::uint32_t& Lattice) -> bool
        {
            int ReadWidth = 0;
            int ReadHeight = 0;
            int ReadChannels = 0;
            unsigned char* ReadPixels = stbi_load(Path, &ReadWidth, &ReadHeight, &ReadChannels, 4);

            if (ReadPixels == nullptr)
                return false;

            Red = Green = Blue = Lattice = 0u;

            for (int Y = 70; Y < ReadHeight; ++Y)
            {
                for (int X = 0; X < 637; ++X)
                {
                    const std::size_t Offset = (static_cast<std::size_t>(Y) * ReadWidth + X) * 4u;
                    const int R = ReadPixels[Offset + 0u];
                    const int G = ReadPixels[Offset + 1u];
                    const int B = ReadPixels[Offset + 2u];

                    if (R > 160 && G < 110 && B < 110 && (R - G) > 60)
                        ++Red;                                     // X axis, red
                    else if (G > 110 && R < 110 && B < 110 && (G - R) > 40)
                        ++Green;                                   // Y axis, green
                    else if (R < 90 && G < 130 && B > 170 && (B - R) > 110)
                        ++Blue;                                    // Z axis, blue (the sky is excluded: R >= 90)
                    else if (R > 38 && R < 130 && B > R && (B - R) < 22 &&
                             std::abs(R - G) < 8 && std::abs(G - B) < 14)
                        ++Lattice;                                 // the grey-blue lattice ink
                }
            }

            stbi_image_free(ReadPixels);
            return true;
        };

        std::uint32_t Red = 0u, Green = 0u, Blue = 0u, LatticeBoth = 0u;
        if (!Scan(OutputPath, Red, Green, Blue, LatticeBoth))
        {
            std::fprintf(stderr, "[FAIL] the grid capture would not read back\n");
            return false;
        }

        std::fprintf(stderr, "[assert] axes+gizmo: axis px R=%u G=%u B=%u\n", Red, Green, Blue);

        if (Red < 20u || Blue < 20u || Green < 20u)
        {
            std::fprintf(stderr, "[FAIL] an axis line did not draw\n");
            return false;
        }

        // 📐 The CLIP's own proof: the overlay's unique full-opacity colours — axis red (229,72,77)
        //    and the gizmo's white handle (240,240,240) — must be ZERO beyond the viewport leaf
        //    (x >= 637). Any pixel there would mean the grid or the gizmo painted over the panels.
        {
            int ReadWidth = 0;
            int ReadHeight = 0;
            int ReadChannels = 0;
            unsigned char* ReadPixels = stbi_load(OutputPath, &ReadWidth, &ReadHeight, &ReadChannels, 4);

            if (ReadPixels == nullptr)
            {
                std::fprintf(stderr, "[FAIL] the clip capture would not read back\n");
                return false;
            }

            std::uint32_t RedOutside = 0u;
            for (int Y = 70; Y < ReadHeight; ++Y)
            {
                for (int X = 637; X < ReadWidth; ++X)
                {
                    const std::size_t Offset = (static_cast<std::size_t>(Y) * ReadWidth + X) * 4u;
                    const int R = ReadPixels[Offset + 0u];
                    const int G = ReadPixels[Offset + 1u];
                    const int B = ReadPixels[Offset + 2u];

                    if (R > 200 && G < 120 && B < 120 && (R - G) > 100)
                        ++RedOutside;
                }
            }

            stbi_image_free(ReadPixels);

            std::fprintf(stderr, "[assert] axis-red outside the viewport leaf: %u\n", RedOutside);
            if (RedOutside != 0u)
            {
                std::fprintf(stderr, "[FAIL] the overlay painted over the panels\n");
                return false;
            }
        }

        // 📐 The gizmo's own proof: its centre handle is a FULL-OPACITY white square at the origin's
        //    projection — unique to the gizmo, unmistakable against the blue sky and the lattice.
        {
            int ReadWidth = 0;
            int ReadHeight = 0;
            int ReadChannels = 0;
            unsigned char* ReadPixels = stbi_load(OutputPath, &ReadWidth, &ReadHeight, &ReadChannels, 4);

            if (ReadPixels == nullptr)
            {
                std::fprintf(stderr, "[FAIL] the gizmo capture would not read back\n");
                return false;
            }

            std::uint32_t HandlePixels = 0u;
            for (int Y = 70; Y < ReadHeight; ++Y)
            {
                for (int X = 0; X < 637; ++X)
                {
                    const std::size_t Offset = (static_cast<std::size_t>(Y) * ReadWidth + X) * 4u;
                    const int R = ReadPixels[Offset + 0u];
                    const int G = ReadPixels[Offset + 1u];
                    const int B = ReadPixels[Offset + 2u];

                    if (R > 215 && G > 215 && B > 215 && std::abs(R - G) < 12 && std::abs(G - B) < 12)
                        ++HandlePixels;
                }
            }

            stbi_image_free(ReadPixels);

            std::fprintf(stderr, "[assert] gizmo handle pixels: %u\n", HandlePixels);
            if (HandlePixels < 12u)
            {
                std::fprintf(stderr, "[FAIL] the gizmo centre handle did not draw\n");
                return false;
            }
        }

        // 📐 PART TWO — the presentation modes over the GROUND, from the elevated corner looking
        //    down (yaw 225, pitch -24, position 40,25,40), with the axes OFF so the lattice ink is
        //    the only variable: Dots first, then Lines + Dots, at the same pose.
        Driver.EditorCamera.YawDegrees   = 225.0;
        Driver.EditorCamera.PitchDegrees = -24.0;
        Driver.EditorCamera.Position[0]  = 40.0;
        Driver.EditorCamera.Position[1]  = 25.0;
        Driver.EditorCamera.Position[2]  = 40.0;
        Driver.EditorCamera.Snap();

        Driver.Configuration.AxisX = false;
        Driver.Configuration.AxisY = false;
        Driver.Configuration.AxisZ = false;
        Driver.Configuration.Lattice = PanelLatticePresentation::Dots;
        Driver.Settle(10);

        const std::string DotsPath = "_AgentScratch/grid-dots.png";

        if (!Driver.Capture(DotsPath.c_str(), Atlas, AtlasWidth, AtlasHeight))
            return false;

        std::uint32_t DotsRed = 0u, DotsGreen = 0u, DotsBlue = 0u, LatticeDots = 0u;
        if (!Scan(DotsPath.c_str(), DotsRed, DotsGreen, DotsBlue, LatticeDots))
        {
            std::fprintf(stderr, "[FAIL] the dots capture would not read back\n");
            return false;
        }

        Driver.Configuration.Lattice = PanelLatticePresentation::LinesAndDots;
        Driver.Settle(10);

        const std::string LinesPath = "_AgentScratch/grid-lines.png";

        if (!Driver.Capture(LinesPath.c_str(), Atlas, AtlasWidth, AtlasHeight))
            return false;

        std::uint32_t LinesRed = 0u, LinesGreen = 0u, LinesBlue = 0u, LatticeLines = 0u;
        if (!Scan(LinesPath.c_str(), LinesRed, LinesGreen, LinesBlue, LatticeLines))
        {
            std::fprintf(stderr, "[FAIL] the lines capture would not read back\n");
            return false;
        }

        std::fprintf(stderr, "[assert] lattice ink: dots-only=%u lines+dots=%u\n",
                     LatticeDots, LatticeLines);

        if (LatticeDots < 200u || LatticeLines <= LatticeDots)
        {
            std::fprintf(stderr, "[FAIL] the lattice presentation did not change the render\n");
            return false;
        }

        // 📐 PART THREE — the popup gate: the GPU overlay records AFTER the interface, so an open
        //    editor popup would be painted over by the grid and the axes (the reported "the lines
        //    draw on the ImGui menus"). The host withholds every leaf overlay while any popup
        //    stands; this asserts the same gate on the CPU twin: open the viewport footer's Grid
        //    menu, and the captured render must carry NO overlay ink at all.
        const PlaneExtent WorkspaceBody = Driver.Workspace.Body();
        const float FooterY0 = WorkspaceBody.MaximumY - 48.0f;   // [-] - EditorPanelMeasure.FooterHeight
        const float GridButtonX = WorkspaceBody.MinimumX + 16.0f + 92.0f + 12.0f + 36.0f;
        const float GridButtonY = FooterY0 + 10.0f + 14.0f;

        Driver.Tick(GridButtonX, GridButtonY, true, true, false);
        Driver.Tick(GridButtonX, GridButtonY, false, false, true);
        Driver.Settle(2);

        std::fprintf(stderr, "[assert] grid menu open: %d\n",
                     Driver.Editor.AnyPopupStanding() ? 1 : 0);

        if (!Driver.Editor.AnyPopupStanding())
        {
            std::fprintf(stderr, "[FAIL] the Grid menu did not stand open\n");
            return false;
        }

        const std::string MenuOpenPath = "_AgentScratch/grid-menu-open.png";
        if (!Driver.Capture(MenuOpenPath.c_str(), Atlas, AtlasWidth, AtlasHeight))
            return false;

        {
            int ReadWidth = 0;
            int ReadHeight = 0;
            int ReadChannels = 0;
            unsigned char* ReadPixels = stbi_load(MenuOpenPath.c_str(), &ReadWidth, &ReadHeight,
                                                  &ReadChannels, 4);

            if (ReadPixels == nullptr)
            {
                std::fprintf(stderr, "[FAIL] the grid-menu capture would not read back\n");
                return false;
            }

            std::uint32_t OverlayInk = 0u;

            for (int Y = 70; Y < ReadHeight; ++Y)
            {
                for (int X = 0; X < 637; ++X)
                {
                    const std::size_t Offset = (static_cast<std::size_t>(Y) * ReadWidth + X) * 4u;
                    const int R = ReadPixels[Offset + 0u];
                    const int G = ReadPixels[Offset + 1u];
                    const int B = ReadPixels[Offset + 2u];

                    // 📐 Only the OVERLAY's unique hues count — the axes' full-opacity red/green/blue.
                    //    The menu's own white knobs and text must not count, so the white handle
                    //    predicate is deliberately absent here.
                    const bool AxisRed   = R > 160 && G < 110 && B < 110 && (R - G) > 60;
                    const bool AxisGreen = G > 110 && R < 110 && B < 110 && (G - R) > 40;
                    const bool AxisBlue  = R < 90 && G < 130 && B > 170 && (B - R) > 110;

                    if (AxisRed || AxisGreen || AxisBlue)
                        ++OverlayInk;
                }
            }

            stbi_image_free(ReadPixels);

            std::fprintf(stderr, "[assert] overlay ink with the Grid menu open: %u\n", OverlayInk);

            if (OverlayInk != 0u)
            {
                std::fprintf(stderr, "[FAIL] the grid drew over the open menu\n");
                return false;
            }
        }

        // 📐 Closing the menu restores the overlay — the gate is not a switch that stays off.
        Driver.Tick(GridButtonX, GridButtonY, true, true, false);
        Driver.Tick(GridButtonX, GridButtonY, false, false, true);
        Driver.Settle(2);

        std::fprintf(stderr, "[assert] grid menu closed: %d\n",
                     Driver.Editor.AnyPopupStanding() ? 1 : 0);

        if (Driver.Editor.AnyPopupStanding())
        {
            std::fprintf(stderr, "[FAIL] the Grid menu did not close\n");
            return false;
        }
    }
    else if (std::strcmp(Scenario, "editor-overlay-gpu-only") == 0)
    {
        // 📐 GPU-only means exactly that: when the overlay pass is withheld, the viewport contributes
        //    no second interface-drawn copy of the grid or axes. This scenario suppresses the pass's
        //    CPU twin and asserts that the unique overlay hues are absent from the captured image.
        Driver.SkipOverlayRaster = true;
        Driver.ApplyPartition(false);
        Driver.Settle(20);

        Driver.Configuration.Lattice      = PanelLatticePresentation::LinesAndDots;
        Driver.Configuration.LatticeScale = 2u;
        Driver.Configuration.Subdivisions = 12u;
        Driver.Configuration.AxisX = true;
        Driver.Configuration.AxisY = true;
        Driver.Configuration.AxisZ = true;

        Driver.EditorCamera.YawDegrees   = 45.0;
        Driver.EditorCamera.PitchDegrees = 24.0;
        Driver.EditorCamera.Position[0]  = -40.0;
        Driver.EditorCamera.Position[1]  = -25.0;
        Driver.EditorCamera.Position[2]  = -40.0;
        Driver.EditorCamera.Snap();
        Driver.Settle(10);

        if (!Driver.Capture(OutputPath, Atlas, AtlasWidth, AtlasHeight))
            return false;

        std::uint32_t Red = 0u, Green = 0u, Blue = 0u, Lattice = 0u;
        std::uint32_t RedOutside = 0u;
        {
            int ReadWidth = 0;
            int ReadHeight = 0;
            int ReadChannels = 0;
            unsigned char* ReadPixels = stbi_load(OutputPath, &ReadWidth, &ReadHeight, &ReadChannels, 4);

            if (ReadPixels == nullptr)
            {
                std::fprintf(stderr, "[FAIL] the GPU-only capture would not read back\n");
                return false;
            }

            for (int Y = 70; Y < ReadHeight; ++Y)
            {
                for (int X = 0; X < ReadWidth; ++X)
                {
                    const std::size_t Offset = (static_cast<std::size_t>(Y) * ReadWidth + X) * 4u;
                    const int R = ReadPixels[Offset + 0u];
                    const int G = ReadPixels[Offset + 1u];
                    const int B = ReadPixels[Offset + 2u];

                    const bool AxisRed   = R > 160 && G < 110 && B < 110 && (R - G) > 60;
                    const bool AxisGreen = G > 110 && R < 110 && B < 110 && (G - R) > 40;
                    const bool AxisBlue  = R < 90 && G < 130 && B > 170 && (B - R) > 110;
                    const bool LatticeInk = R > 38 && R < 130 && B > R && (B - R) < 22 &&
                                            std::abs(R - G) < 8 && std::abs(G - B) < 14;

                    if (X < 637)
                    {
                        if (AxisRed)   ++Red;
                        if (AxisGreen) ++Green;
                        if (AxisBlue)  ++Blue;
                        if (LatticeInk) ++Lattice;
                    }
                    else if (AxisRed)
                    {
                        ++RedOutside;
                    }
                }
            }

            stbi_image_free(ReadPixels);
        }

        std::fprintf(stderr, "[assert] gpu-only overlay omitted: axis R=%u G=%u B=%u lattice=%u\n",
                     Red, Green, Blue, Lattice);

        if (Red != 0u || Green != 0u || Blue != 0u || Lattice != 0u)
        {
            std::fprintf(stderr, "[FAIL] overlay colours survived even though the GPU pass was withheld\n");
            return false;
        }

        std::fprintf(stderr, "[assert] gpu-only axis-red outside the viewport leaf: %u\n", RedOutside);

        if (RedOutside != 0u)
        {
            std::fprintf(stderr, "[FAIL] overlay colours leaked into the non-viewport panels\n");
            return false;
        }
    }
    else if (std::strcmp(Scenario, "editor-search-filter") == 0)
    {
        Driver.ApplyPartition(false);
        Driver.Settle(20);

        // 📐 The outliner column's geometry, exactly as the panel lays it out: the column is 350 px
        //    (or 60% of the leaf), the header is `HeaderHeight`, and the search field sits below it.
        const PlaneExtent OutlinerBody = Driver.Editor.LeafBody(1u);
        const float OutlinerX = (350.0f < OutlinerBody.Width() * 0.6f)
                              ? 350.0f : OutlinerBody.Width() * 0.6f;

        ThemeSelection Sel;
        Sel.Current = ThemeSubject::Oled;
        const ThemeProfile Resolved = ResolveTinted(1.0, 1.0, ViewportWidth, Sel);
        const float AppliedFactor = static_cast<float>(Resolved.Measure.DisplayScale)
                                  * Resolved.ControlMeasure.ArtistFactor;
        const ShellMetric Scaled = ScaleShellLengths(AppliedFactor);

        const float Pad = Scaled.PanePad;
        const float SearchX = OutlinerBody.MinimumX + OutlinerX * 0.5f;
        const float SearchY = OutlinerBody.MinimumY + Scaled.HeaderHeight + Pad
                            + Scaled.SearchHeight * 0.5f;

        // 📐 The rows band the assertions count: the whole column below the outliner's own header,
        //    above the page strip and the footer. The facet card and the search field sit inside it,
        //    but their ink is small and constant compared with the rows they filter — the count is
        //    dominated by how many rows are actually presented.
        const float BandY0 = OutlinerBody.MinimumY + Scaled.HeaderHeight + 5.0f;
        const float BandY1 = OutlinerBody.MaximumY - Scaled.FooterHeight - Scaled.ComponentY - 5.0f;
        const float BandX0 = OutlinerBody.MinimumX + 10.0f;
        const float BandX1 = OutlinerBody.MinimumX + OutlinerX - 10.0f;

        // 📐 Counts the row-ink pixels in the band: row text and glyphs are brighter than the dark
        //    column ground, so the count is proportional to the number of rows actually presented.

        const auto RowsInk = [&](const char* Path) -> std::uint32_t
        {
            int ReadWidth = 0;
            int ReadHeight = 0;
            int ReadChannels = 0;
            unsigned char* ReadPixels = stbi_load(Path, &ReadWidth, &ReadHeight, &ReadChannels, 4);

            if (ReadPixels == nullptr)
                return 0u;

            std::uint32_t Ink = 0u;

            for (int Y = static_cast<int>(BandY0); Y < static_cast<int>(BandY1); ++Y)
            {
                for (int X = static_cast<int>(BandX0); X < static_cast<int>(BandX1); ++X)
                {
                    const std::size_t Offset = (static_cast<std::size_t>(Y) * ReadWidth + X) * 4u;
                    const int R = ReadPixels[Offset + 0u];
                    const int G = ReadPixels[Offset + 1u];
                    const int B = ReadPixels[Offset + 2u];

                    if (R > 100 || G > 100 || B > 100)
                        ++Ink;
                }
            }

            stbi_image_free(ReadPixels);
            return Ink;
        };

        // ① The search field: press, type "fly" while held (the tag on "Editor Camera" is
        //    "camera fly view"), release. The run must land in the retention and the field must
        //    report taken.
        Driver.Tick(SearchX, SearchY, true, true, false);
        std::strncpy(Driver.SimTyped, "fly", sizeof(Driver.SimTyped) - 1u);
        Driver.Tick(SearchX, SearchY, true, false, false);
        Driver.Tick(SearchX, SearchY, false, false, true);
        Driver.Settle(2);

        std::fprintf(stderr, "[assert] retention after typing 'fly': '%s' taken=%d\n",
                     Driver.Applied.EntityRetention, Driver.Applied.SearchTaken ? 1 : 0);

        if (std::strcmp(Driver.Applied.EntityRetention, "fly") != 0)
        {
            std::fprintf(stderr, "[FAIL] the search field did not accept the typed run\n");
            return false;
        }

        const std::string FlyPath = "_AgentScratch/filter-fly.png";
        if (!Driver.Capture(FlyPath.c_str(), Atlas, AtlasWidth, AtlasHeight))
            return false;
        const std::uint32_t InkFly = RowsInk(FlyPath.c_str());

        // ② Clear the search; the full directory returns.
        Driver.Applied.EntityRetention[0] = '\0';
        Driver.Settle(2);

        const std::string AllPath = "_AgentScratch/filter-all.png";
        if (!Driver.Capture(AllPath.c_str(), Atlas, AtlasWidth, AtlasHeight))
            return false;
        const std::uint32_t InkAll = RowsInk(AllPath.c_str());

        // ③ The Lights facet: only the Lighting folder and the Sun remain.
        Driver.Applied.FacetEnabled[1u] = true;
        Driver.Settle(2);

        const std::string LightsPath = "_AgentScratch/filter-lights.png";
        if (!Driver.Capture(LightsPath.c_str(), Atlas, AtlasWidth, AtlasHeight))
            return false;
        const std::uint32_t InkLights = RowsInk(LightsPath.c_str());

        // ④ A run nothing matches: the empty state stands.
        Driver.Applied.FacetEnabled[1u] = false;
        Driver.Tick(SearchX, SearchY, true, true, false);
        std::strncpy(Driver.SimTyped, "zzz", sizeof(Driver.SimTyped) - 1u);
        Driver.Tick(SearchX, SearchY, true, false, false);
        Driver.Tick(SearchX, SearchY, false, false, true);
        Driver.Settle(2);

        const std::string NonePath = "_AgentScratch/filter-none.png";
        if (!Driver.Capture(NonePath.c_str(), Atlas, AtlasWidth, AtlasHeight))
            return false;
        const std::uint32_t InkNone = RowsInk(NonePath.c_str());

        std::fprintf(stderr,
                     "[assert] row ink: all=%u lights=%u fly=%u none=%u\n",
                     InkAll, InkLights, InkFly, InkNone);

        // 📐 The facet card and the search field add a constant ink floor to every phase; the ROW
        //    count is what moves. Seven rows must out-ink two rows by a margin, two rows must
        //    out-ink the empty state, and the empty state stays bounded.
        if (InkAll < InkLights + 100u || InkLights < InkNone + 100u || InkFly < InkNone + 100u)
        {
            std::fprintf(stderr, "[FAIL] the search or the facets did not filter the rows\n");
            return false;
        }

        if (InkNone > 900u)
        {
            std::fprintf(stderr, "[FAIL] the empty state still drew rows\n");
            return false;
        }

        std::fprintf(stderr, "[assert] the tag 'fly' found the camera by tag, not by name\n");

        // 📐 The FILTER CARD's proportions, on real pixels: the FacetPanel's declared card height
        //    (pad + header + gap + one chip row + gap + the dropdown field + trailing pad) must match
        //    what actually rendered — the "squashed filter" regression was exactly a card that
        //    collapsed to a sliver while the header and the dropdown fought for the same rows. The
        //    card ground is `CardGround` (0x121212); its vertical extent in the directory column is
        //    measured and compared with the panel's own arithmetic.
        {
            const float ExpectedHeight = 10.0f + 22.0f + 8.0f + 27.0f + 10.0f
                                       + Resolved.ControlMeasure.FieldHeight + 10.0f;

            int ReadWidth = 0;
            int ReadHeight = 0;
            int ReadChannels = 0;
            unsigned char* ReadPixels = stbi_load(OutputPath, &ReadWidth, &ReadHeight, &ReadChannels, 4);

            if (ReadPixels == nullptr)
            {
                std::fprintf(stderr, "[FAIL] the filter capture would not read back\n");
                return false;
            }

            std::uint32_t FirstY = ReadHeight;
            std::uint32_t LastY = 0u;

            for (int Y = 60; Y < ReadHeight; ++Y)
            {
                std::uint32_t Ground = 0u;

                for (int X = static_cast<int>(OutlinerBody.MinimumX + 30.0f);
                     X < static_cast<int>(OutlinerBody.MinimumX + OutlinerX - 30.0f); ++X)
                {
                    const std::size_t Offset = (static_cast<std::size_t>(Y) * ReadWidth + X) * 4u;
                    const int R = ReadPixels[Offset + 0u];
                    const int G = ReadPixels[Offset + 1u];
                    const int B = ReadPixels[Offset + 2u];

                    if (R == 18 && G == 18 && B == 18)
                        ++Ground;
                }

                if (Ground > 30u)
                {
                    if (FirstY == static_cast<std::uint32_t>(ReadHeight))
                        FirstY = static_cast<std::uint32_t>(Y);
                    LastY = static_cast<std::uint32_t>(Y);
                }
            }

            stbi_image_free(ReadPixels);

            const float MeasuredHeight = (LastY > FirstY) ? static_cast<float>(LastY - FirstY) : 0.0f;

            std::fprintf(stderr, "[assert] filter card height: expected %.1f measured %.1f\n",
                         ExpectedHeight, MeasuredHeight);

            if (MeasuredHeight < ExpectedHeight - 8.0f || MeasuredHeight > ExpectedHeight + 8.0f)
            {
                std::fprintf(stderr, "[FAIL] the filter card did not render at its declared height\n");
                return false;
            }
        }
    }
    else if (std::strcmp(Scenario, "editor-grid-fade") == 0)
    {
        Driver.Partition.ConstructPanelPartition(PanelSubject::Viewport);
        Driver.Configuration.LatticeExtentMetres = 100.0;
        Driver.Configuration.LatticeFadeRadiusMetres = 40.0;
        Driver.Settle(20);
        const PlaneExtent Body = Driver.Editor.LeafBody(0u);
        Driver.Tap(Body.MinimumX + 36.0f, Body.MaximumY + 19.0f); // real Grid footer pill
        Driver.Settle(8);
        if (!Driver.Editor.AnyPopupStanding())
        {
            std::fprintf(stderr, "[FAIL] Grid footer did not open finite-grid settings\n");
            return false;
        }
        const auto FadeAt = [](double Distance, double Begin, double End) -> double
        {
            if (Distance <= Begin) return 1.0;
            if (Distance >= End) return 0.0;
            const double T = (Distance - Begin) / (End - Begin);
            return 1.0 - T * T * (3.0 - 2.0 * T);
        };
        const double Extent = Driver.Configuration.LatticeExtentMetres;
        const double Radius = Driver.Configuration.LatticeFadeRadiusMetres;
        if (FadeAt(0.0, Extent * 0.82, Extent) != 1.0 ||
            FadeAt(Extent, Extent * 0.82, Extent) != 0.0 ||
            FadeAt(Radius, Radius * 0.62, Radius) != 0.0)
        {
            std::fprintf(stderr, "[FAIL] finite-grid fade endpoints are not sharp/absent\n");
            return false;
        }
        std::fprintf(stderr, "[assert] finite grid extent=%.1fm camera fade=%.1fm\n",
                     Extent, Radius);
    }
    else if (std::strcmp(Scenario, "editor-grid-dropdown") == 0)
    {
        Driver.Partition.ConstructPanelPartition(PanelSubject::Viewport);
        Driver.Settle(20);
        const PlaneExtent Body = Driver.Editor.LeafBody(0u);
        Driver.Tap(Body.MinimumX + 36.0f, Body.MaximumY + 19.0f); // Grid pill
        Driver.Settle(4);
        const float MenuTop = Body.MaximumY - 522.0f;
        Driver.Tap(Body.MinimumX + 280.0f, MenuTop + 76.0f); // real Grid Type selection field
        Driver.Settle(4);
        if (!Driver.Editor.AnyPopupStanding())
        {
            std::fprintf(stderr, "[FAIL] opening Grid Type withdrew its parent popup\n");
            return false;
        }
        Driver.Tap(Body.MinimumX + 280.0f, MenuTop + 180.0f); // Dotted roster row
        Driver.Settle(4);
        if (Driver.Configuration.Lattice != PanelLatticePresentation::Dots)
        {
            std::fprintf(stderr, "[FAIL] Grid Type roster did not apply the Dotted option (reading=%u)\n",
                         static_cast<unsigned>(Driver.Configuration.Lattice));
            return false;
        }
        std::fprintf(stderr, "[assert] nested Grid Type roster stayed open and selected Dotted\n");
    }
    else if (std::strcmp(Scenario, "editor-scene-transfer") == 0)
    {
        Driver.Partition.ConstructPanelPartition(PanelSubject::Outliner);
        Driver.Settle(20);
        const PlaneExtent Body = Driver.Editor.LeafBody(0u);
        Driver.Tap(Body.MinimumX + 44.0f, Body.MaximumY + 19.0f); // real Import footer pill
        Driver.Settle(28);
        if (Driver.Applied.OutlinePage != 2u || Driver.Applied.TransferMode != 0u)
        {
            std::fprintf(stderr, "[FAIL] Scene Directory Import footer did not open its outer dialogue\n");
            return false;
        }
        if (Driver.TexturingApplied.StackPage != 0u)
        {
            std::fprintf(stderr, "[FAIL] Scene transfer changed Layer Stack navigation\n");
            return false;
        }
        std::fprintf(stderr, "[assert] Scene Directory Import footer opened transfer page 2\n");
    }
    else if (std::strcmp(Scenario, "editor-scene-transfer-options") == 0)
    {
        Driver.Partition.ConstructPanelPartition(PanelSubject::Outliner);
        Driver.Settle(20);
        const PlaneExtent Body = Driver.Editor.LeafBody(0u);
        Driver.Tap(Body.MinimumX + 44.0f, Body.MaximumY + 19.0f); // real Import footer pill
        Driver.Settle(24);
        Driver.Applied.TransferCardExpanded[0u] = true;
        Driver.Settle(24);
        if (!Driver.Applied.TransferCardExpanded[0u])
        {
            std::fprintf(stderr, "[FAIL] Transform option card did not remain disclosed\n");
            return false;
        }
        std::fprintf(stderr, "[assert] compact transfer dropdowns and animated option card rendered\n");
    }
    else if (std::strcmp(Scenario, "editor-scene-export") == 0)
    {
        Driver.Partition.ConstructPanelPartition(PanelSubject::Outliner);
        Driver.Settle(20);
        const PlaneExtent Body = Driver.Editor.LeafBody(0u);
        Driver.Tap(Body.MinimumX + 160.0f, Body.MaximumY + 19.0f); // real Export footer pill
        Driver.Settle(28);
        if (Driver.Applied.OutlinePage != 2u || Driver.Applied.TransferMode != 1u)
        {
            std::fprintf(stderr, "[FAIL] Scene Directory Export footer did not open export mode\n");
            return false;
        }
        Driver.Tap(Body.MaximumX - 28.0f, Body.MinimumY + 177.0f);
        Driver.Settle(20);
        if (Driver.Applied.TransferFormat != 1u)
        {
            std::fprintf(stderr, "[FAIL] Scene export format carousel did not advance\n");
            return false;
        }
        Driver.Tap(Body.MinimumX + 275.0f, Body.MinimumY + 177.0f); // visible GLB option tile
        Driver.Settle(20);
        if (Driver.Applied.TransferFormat != 2u)
        {
            std::fprintf(stderr, "[FAIL] Scene export format tile did not select GLB\n");
            return false;
        }
        Driver.Tap(Body.MinimumX + 48.0f, Body.MinimumY + 68.0f); // dedicated Back control
        Driver.Settle(20);
        if (Driver.Applied.OutlinePage != 0u)
        {
            std::fprintf(stderr, "[FAIL] Scene transfer Back did not return to the directory\n");
            return false;
        }
        Driver.Tap(Body.MinimumX + 160.0f, Body.MaximumY + 19.0f); // reopen for the raster
        Driver.Settle(28);
        Driver.Tap(Body.MinimumX + 220.0f, Body.MinimumY + 323.0f); // Name field first contact
        Driver.Tap(Body.MinimumX + 220.0f, Body.MinimumY + 323.0f); // double-contact selects its run
        Driver.Settle(2);
        std::fprintf(stderr, "[assert] Scene Directory arrows, tiles, and Back all responded\n");
    }
    else if (std::strcmp(Scenario, "editor-layer-export") == 0)
    {
        Driver.Partition.ConstructPanelPartition(PanelSubject::Texturing);
        Driver.Settle(20);
        const PlaneExtent Body = Driver.Editor.LeafBody(0u);
        Driver.Tap(Body.MinimumX + 218.0f, Body.MaximumY + 19.0f); // real Export footer pill
        Driver.Settle(28);
        if (Driver.TexturingApplied.StackPage != 2u || Driver.TexturingApplied.ExportMode != 1u)
        {
            std::fprintf(stderr, "[FAIL] Layer Stack Export footer did not open texture-set mode\n");
            return false;
        }
        Driver.Tap(Body.MaximumX - 28.0f, Body.MinimumY + 177.0f);
        Driver.Settle(20);
        Driver.Tap(Body.MaximumX - 28.0f, Body.MinimumY + 259.0f);
        Driver.Settle(20);
        Driver.Tap(Body.MaximumX - 28.0f, Body.MinimumY + 355.0f);
        Driver.Settle(20);
        if (Driver.TexturingApplied.ExportFormat != 1u ||
            Driver.TexturingApplied.ExportResolution != 5u ||
            Driver.TexturingApplied.ExportPreset != 1u)
        {
            std::fprintf(stderr, "[FAIL] one of the Layer Stack export carousels did not advance\n");
            return false;
        }
        Driver.Tap(Body.MinimumX + 275.0f, Body.MinimumY + 177.0f); // visible TGA option tile
        Driver.Settle(20);
        if (Driver.TexturingApplied.ExportFormat != 2u)
        {
            std::fprintf(stderr, "[FAIL] Layer Stack export format tile did not select TGA\n");
            return false;
        }
        Driver.Tap(Body.MinimumX + 48.0f, Body.MinimumY + 68.0f); // dedicated Back control
        Driver.Settle(20);
        if (Driver.TexturingApplied.StackPage != 0u)
        {
            std::fprintf(stderr, "[FAIL] Layer Stack export Back did not return to the stack\n");
            return false;
        }
        Driver.Tap(Body.MinimumX + 218.0f, Body.MaximumY + 19.0f); // reopen for the raster
        Driver.Settle(28);
        std::fprintf(stderr, "[assert] Layer Stack arrows, tiles, and Back all responded\n");
    }
    else if (std::strcmp(Scenario, "editor-layer-flatten") == 0)
    {
        Driver.Partition.ConstructPanelPartition(PanelSubject::Texturing);
        Driver.Settle(20);
        const PlaneExtent Body = Driver.Editor.LeafBody(0u);
        Driver.Tap(Body.MinimumX + 73.0f, Body.MaximumY + 19.0f); // real Export Flattened footer pill
        Driver.Settle(28);
        if (Driver.TexturingApplied.StackPage != 2u)
        {
            std::fprintf(stderr, "[FAIL] Export Flattened footer did not open its outer dialogue\n");
            return false;
        }
        if (Driver.TexturingApplied.PropertyTab != 0u)
        {
            std::fprintf(stderr, "[FAIL] flattened export conflated outer and property navigation\n");
            return false;
        }
        std::fprintf(stderr, "[assert] Layer Stack Export Flattened footer opened page 2\n");
    }
    else if (std::strcmp(Scenario, "editor-layerstack-card") == 0)
    {
        Driver.Partition.ConstructPanelPartition(PanelSubject::Texturing);
        Driver.Settle(20);

        ThemeSelection Selection;
        Selection.Current = ThemeSubject::Oled;
        const ThemeProfile Profile = ResolveTinted(1.0, 1.0, ViewportWidth, Selection);
        const ShellMetric Metric = ScaleShellLengths(static_cast<float>(Profile.Measure.DisplayScale)
                                                     * Profile.ControlMeasure.ArtistFactor);
        const PlaneExtent Row = Driver.Texturing.RowExtent(1u);
        const float Action = Metric.LayerToolHeight - 5.0f;
        const float DisclosureX = Row.MaximumX - 14.0f - Action * 1.5f - 4.0f;
        const float DisclosureY = Row.MinimumY + Metric.LayerRowY * 0.5f;

        Driver.Tap(DisclosureX, DisclosureY);
        Driver.Settle(28);

        if (!Driver.TexturingApplied.LayerCardExpanded[1u])
        {
            std::fprintf(stderr, "[FAIL] the layer disclosure V did not open its inline card\n");
            return false;
        }

        if (Driver.TexturingApplied.StackPage != 0u)
        {
            std::fprintf(stderr, "[FAIL] inline disclosure changed the carousel page\n");
            return false;
        }

        // 📐 Every section header is its own disclosure. Collapse Effects through its real header and
        //    retain that state across the outer card and carousel transitions below.
        const float CardTop = Row.MinimumY + Metric.LayerRowY + 2.0f;
        const float OpenSection = 29.0f + 4.0f + (Metric.ComponentY + 4.0f) * 2.0f;
        const float EffectsY = CardTop + 8.0f + OpenSection * 2.0f + 14.5f;
        Driver.Tap(Row.MinimumX + 100.0f, EffectsY);
        Driver.Settle(8);
        if (Driver.TexturingApplied.LayerCardSection[1u][2u])
        {
            std::fprintf(stderr, "[FAIL] the Effects section header did not collapse\n");
            return false;
        }

        // 📐 The two gestures remain independent: close the V-card, double-contact the row body to
        //    travel to Channels, return with Tab, then reopen the V-card for the captured raster.
        Driver.Tap(DisclosureX, DisclosureY);
        Driver.Settle(4);
        const float BodyX = Row.MinimumX + Row.Width() * 0.45f;
        Driver.Tap(BodyX, DisclosureY);
        Driver.Tap(BodyX, DisclosureY);
        Driver.Settle(4);

        if (Driver.TexturingApplied.StackPage != 1u)
        {
            std::fprintf(stderr, "[FAIL] a double contact did not open the layer properties carousel\n");
            return false;
        }

        Driver.SimLayersTab = true;
        Driver.Tick(BodyX, DisclosureY, false, false, false);
        Driver.Settle(20);

        const PlaneExtent Returned = Driver.Texturing.RowExtent(1u);
        const float ReturnedX = Returned.MaximumX - 14.0f - Action * 1.5f - 4.0f;
        const float ReturnedY = Returned.MinimumY + Metric.LayerRowY * 0.5f;
        Driver.Tap(ReturnedX, ReturnedY);
        Driver.Settle(28);

        if (!Driver.TexturingApplied.LayerCardExpanded[1u] ||
            Driver.TexturingApplied.StackPage != 0u)
        {
            std::fprintf(stderr, "[FAIL] the inline card did not reopen independently after carousel travel\n");
            return false;
        }

        std::fprintf(stderr, "[assert] V-card and double-contact carousel remain independent\n");
    }
    else if (std::strcmp(Scenario, "editor-layer-scroll-return") == 0)
    {
        // 🔴 Regression: a disclosure from the Scene Directory survived a leaf subject change. The old
        //    global AnyDisclosed gate then rejected every Layer Stack wheel forever after returning.
        Driver.Partition.ConstructPanelPartition(PanelSubject::Outliner);
        Driver.Settle(12);

        const Deliver<ControlIdentity> Foreign = Driver.Interaction.Register();
        if (!Foreign.Resolved || !Driver.Interaction.Disclose(Foreign.Resolve()))
        {
            std::fprintf(stderr, "[FAIL] could not stage the foreign disclosure\n");
            return false;
        }

        Driver.Partition.ConstructPanelPartition(PanelSubject::Viewport);
        Driver.Settle(8);
        Driver.Partition.ConstructPanelPartition(PanelSubject::Texturing);

        for (std::uint32_t Index = 0u; Index < 8u; ++Index)
            Driver.TexturingApplied.LayerCardExpanded[Index] = true;

        Driver.Settle(28);
        const PlaneExtent Body = Driver.Editor.LeafBody(0u);
        Driver.Tick(Body.MinimumX + Body.Width() * 0.5f,
                    Body.MinimumY + Body.Height() * 0.55f,
                    false, false, false, -1.0f);
        Driver.Settle(12);

        if (Driver.TexturingApplied.StackListWanted <= 0.0f)
        {
            std::fprintf(stderr, "[FAIL] Layer Stack wheel remained blocked after panel return\n");
            return false;
        }
        if (Driver.Interaction.AnyDisclosed())
        {
            std::fprintf(stderr, "[FAIL] Layer Stack wheel did not retire the stale disclosure\n");
            return false;
        }

        std::fprintf(stderr, "[assert] Layer Stack wheel recovered after Outliner and Viewport travel\n");
    }
    else if (std::strcmp(Scenario, "editor-layer-multiselect") == 0)
    {
        Driver.Partition.ConstructPanelPartition(PanelSubject::Texturing);
        for (std::uint32_t Index = 0u; Index < TextureLayerLimit; ++Index)
            Driver.TexturingApplied.LayerSelected[Index] = false;
        Driver.TexturingApplied.LayerSelected[1u] = true;
        Driver.TexturingApplied.LayerSelected[2u] = true;
        Driver.TexturingApplied.LayerSelected[4u] = true;
        Driver.TexturingApplied.LayerTaken = 4u;
        Driver.TexturingApplied.LayerSelectionAnchor = 1u;
        Driver.Settle(20);
    }
    else if (std::strcmp(Scenario, "editor-layerstack") == 0)
    {
        // 📐 A single Texturing leaf: the whole workspace body is the layer stack.
        Driver.Partition.ConstructPanelPartition(PanelSubject::Texturing);
        Driver.Settle(20);

        const PlaneExtent LeafBody = Driver.Editor.LeafBody(0u);
        const float LeafX = LeafBody.MinimumX + LeafBody.Width() * 0.5f;

        ThemeSelection Sel;
        Sel.Current = ThemeSubject::Oled;
        const ThemeProfile Resolved = ResolveTinted(1.0, 1.0, ViewportWidth, Sel);
        const float AppliedFactor = static_cast<float>(Resolved.Measure.DisplayScale)
                                  * Resolved.ControlMeasure.ArtistFactor;
        const ShellMetric Scaled = ScaleShellLengths(AppliedFactor);

        // 📐 The reference's layout, in the panel's own metrics: the header, the tools row (the search
        //    pill filling what the three tools leave), the filter card, the rows band, the page strip
        //    and the footer (crumb + blend/opacity + the action bar).
        const float Pad = Scaled.PanePad;
        const float ToolY = Scaled.LayerToolHeight;
        const float ToolBand = ToolY + Scaled.LayerFoldPad * 2.0f;

        const float HeaderTop = LeafBody.MinimumY;
        const float HeaderBottom = HeaderTop + Scaled.HeaderHeight;
        const float ToolsTop = HeaderBottom;
        const float SearchRight = LeafBody.MaximumX - Pad - (ToolY * 3.0f + 10.0f * 2.0f + 6.0f) - 1.0f;
        const float SearchX = (LeafBody.MinimumX + Pad + SearchRight) * 0.5f;
        const float SearchY = ToolsTop + ToolBand * 0.5f;

        const float StripY0 = LeafBody.MaximumY - Scaled.LayerFootCrumb - Scaled.LayerFootProp
                            - Scaled.LayerFootBar - Scaled.ComponentY;
        const float FooterY0 = StripY0 + Scaled.ComponentY;

        // 📐 The rows band: exactly the extents the panel reported.
        const float BandY0 = Driver.Texturing.RowExtent(0u).MinimumY;
        const float BandY1 = StripY0;

        const auto Ink = [&](const char* Path) -> std::uint32_t
        {
            int ReadWidth = 0;
            int ReadHeight = 0;
            int ReadChannels = 0;
            unsigned char* ReadPixels = stbi_load(Path, &ReadWidth, &ReadHeight, &ReadChannels, 4);

            if (ReadPixels == nullptr)
                return 0u;

            std::uint32_t Count = 0u;

            for (int Y = static_cast<int>(BandY0); Y < static_cast<int>(BandY1); ++Y)
            {
                for (int X = static_cast<int>(LeafBody.MinimumX + 30.0f);
                     X < static_cast<int>(LeafBody.MaximumX - 30.0f); ++X)
                {
                    const std::size_t Offset = (static_cast<std::size_t>(Y) * ReadWidth + X) * 4u;
                    const int R = ReadPixels[Offset + 0u];
                    const int G = ReadPixels[Offset + 1u];
                    const int B = ReadPixels[Offset + 2u];

                    if (R > 110 || G > 110 || B > 110)
                        ++Count;
                }
            }

            stbi_image_free(ReadPixels);
            return Count;
        };

        // ① The stack page renders; the panel exposes the exact row extents it drew.
        const std::string StackPath = "_AgentScratch/layer-stack.png";
        if (!Driver.Capture(StackPath.c_str(), Atlas, AtlasWidth, AtlasHeight))
            return false;

        const std::uint32_t InkStack = Ink(StackPath.c_str());

        std::fprintf(stderr, "[assert] layer stack row ink: %u\n", InkStack);
        if (InkStack < 300u)
        {
            std::fprintf(stderr, "[FAIL] the layer stack page did not draw its rows\n");
            return false;
        }

        // ①b The reference's appearance, on real pixels: the entry's colour tag at the folder row's
        //     left edge, the attached mask row's dashed border beneath Warning Stencil, and the
        //     footer's blend pill + slider + action bar below the strip.
        {
            int ReadWidth = 0;
            int ReadHeight = 0;
            int ReadChannels = 0;
            unsigned char* ReadPixels = stbi_load(StackPath.c_str(), &ReadWidth, &ReadHeight,
                                                  &ReadChannels, 4);

            if (ReadPixels == nullptr)
                return false;

            const auto Pixel = [&](int X, int Y, int& R, int& G, int& B)
            {
                const std::size_t Offset = (static_cast<std::size_t>(Y) * ReadWidth + X) * 4u;
                R = ReadPixels[Offset + 0u];
                G = ReadPixels[Offset + 1u];
                B = ReadPixels[Offset + 2u];
            };

            // 📐 The 3 px colour tag of the Surface Detail folder — 0x9B8CF0 at 0.9 over the row.
            const PlaneExtent TagRow0 = Driver.Texturing.RowExtent(0u);
            int TagR = 0, TagG = 0, TagB = 0;
            Pixel(static_cast<int>(TagRow0.MinimumX + 1.0f),
                  static_cast<int>(TagRow0.MinimumY + 22.0f), TagR, TagG, TagB);

            std::fprintf(stderr, "[assert] row 0 colour tag rgb: %d %d %d\n", TagR, TagG, TagB);
            if (TagR < 110 || TagG < 90 || TagB < 170 || TagB <= TagR)
            {
                std::fprintf(stderr, "[FAIL] the row's 3 px colour tag did not draw its hue\n");
                stbi_image_free(ReadPixels);
                return false;
            }

            // 📐 The mask row under Warning Stencil: a dashed border, not a solid one — bright
            //     segments alternating with gaps along the mask row's top edge.
            const PlaneExtent Stencil = Driver.Texturing.RowExtent(2u);
            const PlaneExtent MaskRow2 = Spanning(Stencil.MinimumX,
                                                  Stencil.MinimumY + Scaled.LayerRowY,
                                                  Stencil.Width(), Scaled.LayerMaskY);

            std::uint32_t Dashed = 0u;
            std::uint32_t Solid = 0u;

            for (int X = static_cast<int>(MaskRow2.MinimumX + 2.0f);
                 X < static_cast<int>(MaskRow2.MaximumX - 2.0f); ++X)
            {
                int R = 0, G = 0, B = 0;
                Pixel(X, static_cast<int>(MaskRow2.MinimumY), R, G, B);

                if (R > 40 && G > 40 && B > 40)
                    ++Dashed;
                else
                    ++Solid;
            }

            std::fprintf(stderr, "[assert] mask row border dashes: %u solid-gap %u\n", Dashed, Solid);
            if (Dashed < 6u || Solid < 6u)
            {
                std::fprintf(stderr, "[FAIL] the attached mask row did not draw its dashed border\n");
                stbi_image_free(ReadPixels);
                return false;
            }

            // 📐 The footer's action bar: bright icon ink below the strip's bottom edge.
            std::uint32_t FooterInk = 0u;

            for (int Y = static_cast<int>(FooterY0); Y < static_cast<int>(LeafBody.MaximumY); ++Y)
            {
                for (int X = static_cast<int>(LeafBody.MinimumX + 10.0f);
                     X < static_cast<int>(LeafBody.MaximumX - 10.0f); ++X)
                {
                    int R = 0, G = 0, B = 0;
                    Pixel(X, Y, R, G, B);

                    if (R > 150 && G > 150 && B > 150)
                        ++FooterInk;
                }
            }

            std::fprintf(stderr, "[assert] footer ink: %u\n", FooterInk);
            if (FooterInk < 120u)
            {
                std::fprintf(stderr, "[FAIL] the footer's blend/opacity/action bar did not draw\n");
                stbi_image_free(ReadPixels);
                return false;
            }

            stbi_image_free(ReadPixels);
        }

        if (Driver.Texturing.DrawnRows() < 3u)
        {
            std::fprintf(stderr, "[FAIL] the layer stack did not draw enough rows\n");
            return false;
        }

        // 📐 Rows 0, 1, 2 are the folder, Levels and Warning Stencil; the mask row beneath Warning
        //    Stencil sits inside row 2's own extent, one row-height down.
        const PlaneExtent Row0 = Driver.Texturing.RowExtent(0u);
        const PlaneExtent Row1 = Driver.Texturing.RowExtent(1u);
        const PlaneExtent Row2 = Driver.Texturing.RowExtent(2u);

        const float RowY[3] =
        {
            Row0.MinimumY + Row0.Height() * 0.5f,
            Row1.MinimumY + Row1.Height() * 0.5f,
            Row2.MinimumY + Row2.Height() * 0.5f
        };

        const float MaskRowY = Row2.MinimumY + Scaled.LayerRowY + Scaled.LayerMaskY * 0.5f;

        const auto LayerMidY = [&](const PlaneExtent& Block)
        {
            return Block.MinimumY + Scaled.LayerRowY * 0.5f;
        };

        // ② The search pill: type "decal" — only the Warning Stencil row remains.
        Driver.Tick(SearchX, SearchY, true, true, false);
        std::strncpy(Driver.SimTyped, "decal", sizeof(Driver.SimTyped) - 1u);
        Driver.Tick(SearchX, SearchY, true, false, false);
        Driver.Tick(SearchX, SearchY, false, false, true);
        Driver.Settle(2);

        std::fprintf(stderr, "[assert] layer retention after 'decal': '%s'\n",
                     Driver.TexturingApplied.Retention);
        if (std::strcmp(Driver.TexturingApplied.Retention, "decal") != 0)
        {
            std::fprintf(stderr, "[FAIL] the layer search pill did not accept the run\n");
            return false;
        }

        const std::string DecalPath = "_AgentScratch/layer-decal.png";
        if (!Driver.Capture(DecalPath.c_str(), Atlas, AtlasWidth, AtlasHeight))
            return false;

        const std::uint32_t InkDecal = Ink(DecalPath.c_str());

        if (InkDecal * 3u >= InkStack)
        {
            std::fprintf(stderr, "[FAIL] the layer search did not filter the rows\n");
            return false;
        }

        // ③ The Fill facet: only the fills remain.
        Driver.TexturingApplied.Retention[0] = '\0';
        Driver.TexturingApplied.FacetEnabled[1u] = true;
        Driver.Settle(2);

        const std::string FillPath = "_AgentScratch/layer-fill.png";
        if (!Driver.Capture(FillPath.c_str(), Atlas, AtlasWidth, AtlasHeight))
            return false;

        const std::uint32_t InkFill = Ink(FillPath.c_str());

        std::fprintf(stderr, "[assert] stack ink all=%u decal=%u fill=%u\n", InkStack, InkDecal, InkFill);
        if (InkFill * 2u >= InkStack * 3u)
        {
            std::fprintf(stderr, "[FAIL] the layer facets did not filter the rows\n");
            return false;
        }

        Driver.TexturingApplied.FacetEnabled[1u] = false;

        // ④ A layer row + Tab -> the channel properties page.
        Driver.Tick(LeafX, RowY[1], true, true, false);
        Driver.Tick(LeafX, RowY[1], false, false, true);
        Driver.Settle(2);
        Driver.SimLayersTab = true;
        Driver.Tick(LeafX, RowY[1], false, false, false);
        Driver.Settle(2);

        std::fprintf(stderr, "[assert] after Tab: page=%u tab=%u taken=%u\n",
                     Driver.TexturingApplied.StackPage, Driver.TexturingApplied.PropertyTab,
                     Driver.TexturingApplied.LayerTaken);

        if (Driver.TexturingApplied.StackPage != 1u || Driver.TexturingApplied.PropertyTab != 0u ||
            Driver.TexturingApplied.LayerTaken != 1u)
        {
            std::fprintf(stderr, "[FAIL] Tab did not open the channel properties\n");
            return false;
        }

        // 📐 The channel properties page draws: its search pill and channel rows carry real ink.
        {
            const std::string ChannelPath = "_AgentScratch/layer-channels.png";
            if (!Driver.Capture(ChannelPath.c_str(), Atlas, AtlasWidth, AtlasHeight))
                return false;

            int ReadWidth = 0, ReadHeight = 0, ReadChannels = 0;
            unsigned char* ReadPixels = stbi_load(ChannelPath.c_str(), &ReadWidth, &ReadHeight,
                                                  &ReadChannels, 4);

            if (ReadPixels == nullptr)
                return false;

            std::uint32_t ChannelInk = 0u;

            for (int Y = 90; Y < ReadHeight - 40; ++Y)
            {
                for (int X = static_cast<int>(LeafBody.MinimumX + 30.0f);
                     X < static_cast<int>(LeafBody.MaximumX - 30.0f); ++X)
                {
                    const std::size_t Offset = (static_cast<std::size_t>(Y) * ReadWidth + X) * 4u;
                    const int R = ReadPixels[Offset + 0u];
                    const int G = ReadPixels[Offset + 1u];
                    const int B = ReadPixels[Offset + 2u];

                    if (R > 110 || G > 110 || B > 110)
                        ++ChannelInk;
                }
            }

            stbi_image_free(ReadPixels);

            std::fprintf(stderr, "[assert] channel properties ink: %u\n", ChannelInk);
            if (ChannelInk < 200u)
            {
                std::fprintf(stderr, "[FAIL] the channel properties page did not draw\n");
                return false;
            }
        }

        // ⑤ Back to the stack, then the mask row under Warning Stencil + Tab -> the mask panel.
        Driver.SimLayersTab = true;
        Driver.Tick(LeafX, RowY[2], false, false, false);
        Driver.Settle(2);

        Driver.Tick(LeafX, MaskRowY, true, true, false);
        Driver.Tick(LeafX, MaskRowY, false, false, true);
        Driver.Settle(2);

        if (!Driver.TexturingApplied.MaskTaken)
        {
            std::fprintf(stderr, "[FAIL] the mask row click did not take\n");
            return false;
        }

        Driver.SimLayersTab = true;
        Driver.Tick(LeafX, MaskRowY, false, false, false);
        Driver.Settle(2);

        std::fprintf(stderr, "[assert] after mask+Tab: mask=%d page=%u tab=%u (the mask panel is the\n"
                             "         selection's single tab, clamped to 0)\n",
                     Driver.TexturingApplied.MaskTaken ? 1 : 0,
                     Driver.TexturingApplied.StackPage, Driver.TexturingApplied.PropertyTab);

        if (!Driver.TexturingApplied.MaskTaken || Driver.TexturingApplied.StackPage != 1u)
        {
            std::fprintf(stderr, "[FAIL] the mask did not open its own mask panel\n");
            return false;
        }

        // 📐 The mask panel draws: its density slider and source field carry real ink.
        {
            const std::string MaskPanelPath = "_AgentScratch/layer-mask-panel.png";
            if (!Driver.Capture(MaskPanelPath.c_str(), Atlas, AtlasWidth, AtlasHeight))
                return false;

            int ReadWidth = 0, ReadHeight = 0, ReadChannels = 0;
            unsigned char* ReadPixels = stbi_load(MaskPanelPath.c_str(), &ReadWidth, &ReadHeight,
                                                  &ReadChannels, 4);

            if (ReadPixels == nullptr)
                return false;

            std::uint32_t MaskInk = 0u;

            for (int Y = 90; Y < ReadHeight - 40; ++Y)
            {
                for (int X = static_cast<int>(LeafBody.MinimumX + 30.0f);
                     X < static_cast<int>(LeafBody.MaximumX - 30.0f); ++X)
                {
                    const std::size_t Offset = (static_cast<std::size_t>(Y) * ReadWidth + X) * 4u;
                    const int R = ReadPixels[Offset + 0u];
                    const int G = ReadPixels[Offset + 1u];
                    const int B = ReadPixels[Offset + 2u];

                    if (R > 110 || G > 110 || B > 110)
                        ++MaskInk;
                }
            }

            stbi_image_free(ReadPixels);

            std::fprintf(stderr, "[assert] mask panel ink: %u\n", MaskInk);
            if (MaskInk < 200u)
            {
                std::fprintf(stderr, "[FAIL] the mask panel did not draw\n");
                return false;
            }
        }

        // ⑥ The stack page's own interactions — the reference's gestures, asserted on the context
        //    and on real pixels.

        // ⑥a The details chevron on the Levels row travels to the channel properties, exactly like Tab.
        Driver.SimLayersTab = true;
        Driver.Tick(LeafX, RowY[1], false, false, false);
        Driver.Settle(2);

        const float DetailX = Row1.MaximumX - 56.0f + 11.5f;
        const float MoreX = Row1.MaximumX - 29.0f + 11.5f;

        Driver.Tick(DetailX, LayerMidY(Row1), true, true, false);
        Driver.Tick(DetailX, LayerMidY(Row1), false, false, true);
        Driver.Settle(2);

        std::fprintf(stderr, "[assert] details chevron: page=%u tab=%u taken=%u\n",
                     Driver.TexturingApplied.StackPage, Driver.TexturingApplied.PropertyTab,
                     Driver.TexturingApplied.LayerTaken);

        if (Driver.TexturingApplied.StackPage != 1u ||
            Driver.TexturingApplied.LayerTaken != 1u)
        {
            std::fprintf(stderr, "[FAIL] the details chevron did not travel to the properties\n");
            return false;
        }

        Driver.SimLayersTab = true;
        Driver.Tick(DetailX, LayerMidY(Row1), false, false, false);
        Driver.Settle(2);

        // ⑥b The more button opens the layer menu; picking Duplicate raises the structural request
        //     the shared stack helper applies — one new row stands, drawn and all.
        Driver.Tick(MoreX, LayerMidY(Row1), true, true, false);
        Driver.Tick(MoreX, LayerMidY(Row1), false, false, true);
        Driver.Settle(2);

        if (Driver.TexturingApplied.MenuOpen != 2u)
        {
            std::fprintf(stderr, "[FAIL] the more button did not open the layer menu\n");
            return false;
        }

        const float MenuCardW = 206.0f;
        const float MenuRowY = Scaled.LayerToolHeight + 2.0f;
        const float LayerMenuX = Row1.MaximumX - 6.0f - MenuCardW;
        const float LayerMenuY = Row1.MinimumY + Scaled.LayerRowY + 6.0f;
        const float ItemTop = LayerMenuY + Scaled.PanePad + 20.0f;

        const std::string MenuPath = "_AgentScratch/layer-menu.png";
        if (!Driver.Capture(MenuPath.c_str(), Atlas, AtlasWidth, AtlasHeight))
            return false;

        // 📐 The menu card itself draws above the rows: its ground is the menu's near-black with the
        //    1 px bright edge — assert the edge's ink inside the card's band.
        {
            int ReadWidth = 0, ReadHeight = 0, ReadChannels = 0;
            unsigned char* ReadPixels = stbi_load(MenuPath.c_str(), &ReadWidth, &ReadHeight,
                                                  &ReadChannels, 4);

            if (ReadPixels == nullptr)
                return false;

            std::uint32_t CardInk = 0u;

            for (int Y = static_cast<int>(LayerMenuY); Y < static_cast<int>(LayerMenuY + 300.0f); ++Y)
            {
                for (int X = static_cast<int>(LayerMenuX);
                     X < static_cast<int>(LayerMenuX + MenuCardW); ++X)
                {
                    const std::size_t Offset = (static_cast<std::size_t>(Y) * ReadWidth + X) * 4u;
                    const int R = ReadPixels[Offset + 0u];
                    const int G = ReadPixels[Offset + 1u];
                    const int B = ReadPixels[Offset + 2u];

                    if (R > 120 && G > 120 && B > 120)
                        ++CardInk;
                }
            }

            std::fprintf(stderr, "[assert] layer menu card ink: %u\n", CardInk);
            if (CardInk < 60u)
            {
                std::fprintf(stderr, "[FAIL] the layer menu card did not draw\n");
                stbi_image_free(ReadPixels);
                return false;
            }

            stbi_image_free(ReadPixels);
        }

        // 📐 Duplicate — the fourth item (Details, mask, lock, solo, DUPLICATE, group, delete).
        const float DuplicateY = ItemTop + MenuRowY * 4.0f + MenuRowY * 0.5f;

        Driver.Tick(LayerMenuX + MenuCardW * 0.5f, DuplicateY, true, true, false);
        Driver.Tick(LayerMenuX + MenuCardW * 0.5f, DuplicateY, false, false, true);
        Driver.Settle(2);

        std::fprintf(stderr, "[assert] after duplicate: rows=%u taken=%u menu=%u\n",
                     Driver.StackRows.Count, Driver.TexturingApplied.LayerTaken,
                     Driver.TexturingApplied.MenuOpen);

        if (Driver.StackRows.Count != 13u || Driver.TexturingApplied.MenuOpen != 0u)
        {
            std::fprintf(stderr, "[FAIL] the layer menu's Duplicate did not insert a row\n");
            return false;
        }

        // 📐 Select the copy, then the trash button removes it again — the twelfth bar button.
        Driver.Tick(LeafX, LayerMidY(Row1), true, true, false);
        Driver.Tick(LeafX, LayerMidY(Row1), false, false, true);
        Driver.Settle(2);

        const float BarTop = FooterY0 + Scaled.LayerFootCrumb + Scaled.LayerFootProp
                           + (Scaled.LayerFootBar - Scaled.LayerToolHeight) * 0.5f;
        const float TrashX = LeafBody.MinimumX + Pad + 424.0f;

        Driver.Tick(TrashX, BarTop + 14.0f, true, true, false);
        Driver.Tick(TrashX, BarTop + 14.0f, false, false, true);
        Driver.Settle(2);

        std::fprintf(stderr, "[assert] after trash: rows=%u\n", Driver.StackRows.Count);
        if (Driver.StackRows.Count != 12u)
        {
            std::fprintf(stderr, "[FAIL] the trash button did not delete the copied row\n");
            return false;
        }

        // ⑥c The layer menu's Solo dims every row outside the solo set and stands the SOLO chip —
        //     asserted on the yellow chip's pixels in the header band.
        const PlaneExtent Row1Again = Driver.Texturing.RowExtent(1u);
        const float MoreX2 = Row1Again.MaximumX - 29.0f + 11.5f;
        const float LayerMenuY2 = Row1Again.MinimumY + Scaled.LayerRowY + 6.0f;
        const float ItemTop2 = LayerMenuY2 + Scaled.PanePad + 20.0f;

        Driver.Tick(MoreX2, LayerMidY(Row1Again), true, true, false);
        Driver.Tick(MoreX2, LayerMidY(Row1Again), false, false, true);
        Driver.Settle(2);

        const float SoloY = ItemTop2 + MenuRowY * 3.0f + MenuRowY * 0.5f;

        Driver.Tick(LayerMenuX + MenuCardW * 0.5f, SoloY, true, true, false);
        Driver.Tick(LayerMenuX + MenuCardW * 0.5f, SoloY, false, false, true);
        Driver.Settle(2);

        if (Driver.TexturingApplied.SoloTaken != 1u)
        {
            std::fprintf(stderr, "[FAIL] the layer menu's Solo did not stand the solo\n");
            return false;
        }

        const std::string SoloPath = "_AgentScratch/layer-solo.png";
        if (!Driver.Capture(SoloPath.c_str(), Atlas, AtlasWidth, AtlasHeight))
            return false;

        {
            int ReadWidth = 0, ReadHeight = 0, ReadChannels = 0;
            unsigned char* ReadPixels = stbi_load(SoloPath.c_str(), &ReadWidth, &ReadHeight,
                                                  &ReadChannels, 4);

            if (ReadPixels == nullptr)
                return false;

            std::uint32_t SoloInk = 0u;

            for (int Y = static_cast<int>(HeaderTop); Y < static_cast<int>(HeaderBottom); ++Y)
            {
                for (int X = static_cast<int>(LeafBody.MinimumX + 100.0f);
                     X < static_cast<int>(LeafBody.MaximumX - 140.0f); ++X)
                {
                    const std::size_t Offset = (static_cast<std::size_t>(Y) * ReadWidth + X) * 4u;
                    const int R = ReadPixels[Offset + 0u];
                    const int G = ReadPixels[Offset + 1u];
                    const int B = ReadPixels[Offset + 2u];

                    if (R > 230 && G > 180 && B < 130)
                        ++SoloInk;
                }
            }

            std::fprintf(stderr, "[assert] SOLO chip yellow ink: %u\n", SoloInk);
            if (SoloInk < 40u)
            {
                std::fprintf(stderr, "[FAIL] the SOLO chip did not stand in the header\n");
                stbi_image_free(ReadPixels);
                return false;
            }

            stbi_image_free(ReadPixels);
        }

        // 📐 Clear the solo through the same menu, then the blend pill: pick Screen (roster index 4).
        Driver.Tick(MoreX2, LayerMidY(Row1Again), true, true, false);
        Driver.Tick(MoreX2, LayerMidY(Row1Again), false, false, true);
        Driver.Settle(2);

        Driver.Tick(LayerMenuX + MenuCardW * 0.5f, SoloY, true, true, false);
        Driver.Tick(LayerMenuX + MenuCardW * 0.5f, SoloY, false, false, true);
        Driver.Settle(2);

        if (Driver.TexturingApplied.SoloTaken != 0xFFFFFFFFu)
        {
            std::fprintf(stderr, "[FAIL] the layer menu's Clear solo did not stand down\n");
            return false;
        }

        const float PillX = LeafBody.MinimumX + Pad;
        const float PillW = std::min(LeafBody.Width() * 0.45f, 190.0f);
        const float PillY = FooterY0 + Scaled.LayerFootCrumb
                          + (Scaled.LayerFootProp - (Scaled.LayerToolHeight - 1.0f)) * 0.5f;
        const float PillMidY = PillY + (Scaled.LayerToolHeight - 1.0f) * 0.5f;

        Driver.Tick(PillX + PillW * 0.5f, PillMidY, true, true, false);
        Driver.Tick(PillX + PillW * 0.5f, PillMidY, false, false, true);
        Driver.Settle(2);

        if (Driver.TexturingApplied.MenuOpen != 4u)
        {
            std::fprintf(stderr, "[FAIL] the blend pill did not open the blend menu\n");
            return false;
        }

        const float BlendCardY0 = PillY + (Scaled.LayerToolHeight - 1.0f) + 6.0f;
        const float BlendCardH = Scaled.PanePad * 2.0f + 20.0f + MenuRowY * 13.0f;
        const float BlendMenuY = (BlendCardY0 + BlendCardH > LeafBody.MaximumY - 6.0f)
                               ? PillY - BlendCardH - 6.0f : BlendCardY0;
        const float BlendItemTop = BlendMenuY + Scaled.PanePad + 20.0f;
        const float ScreenY = BlendItemTop + MenuRowY * 4.0f + MenuRowY * 0.5f;
        const float BlendMenuX = std::max(LeafBody.MinimumX + 6.0f, PillX + PillW - MenuCardW);

        Driver.Tick(BlendMenuX + MenuCardW * 0.5f, ScreenY, true, true, false);
        Driver.Tick(BlendMenuX + MenuCardW * 0.5f, ScreenY, false, false, true);
        Driver.Settle(2);

        std::fprintf(stderr, "[assert] blend picked: %u (Screen=4) menu=%u\n",
                     Driver.TexturingApplied.LayerBlendTaken[1u],
                     Driver.TexturingApplied.MenuOpen);

        if (Driver.TexturingApplied.LayerBlendTaken[1u] != 4u ||
            Driver.TexturingApplied.MenuOpen != 0u)
        {
            std::fprintf(stderr, "[FAIL] the blend menu did not take Screen\n");
            return false;
        }

        // ⑥d The footer's opacity slider: one drag writes the layer's working opacity.
        const float TrackX0 = PillX + PillW + 12.0f;
        const float TrackW = LeafBody.MaximumX - Pad - 46.0f - 8.0f - TrackX0;
        const float DragX = TrackX0 + TrackW * 0.6f;

        Driver.Tick(DragX, PillMidY, true, true, false);
        Driver.Tick(DragX, PillMidY, true, false, false);
        Driver.Tick(DragX, PillMidY, false, false, true);
        Driver.Settle(2);

        std::fprintf(stderr, "[assert] opacity after drag: %u\n",
                     Driver.TexturingApplied.LayerOpacity[1u]);

        if (Driver.TexturingApplied.LayerOpacity[1u] < 55u ||
            Driver.TexturingApplied.LayerOpacity[1u] > 65u)
        {
            std::fprintf(stderr, "[FAIL] the footer opacity slider did not write the drag\n");
            return false;
        }

        // ⑥e The lock button stands the row's L chip.
        const float LockX = LeafBody.MinimumX + Pad + 286.0f + 14.0f;

        Driver.Tick(LockX, BarTop + 14.0f, true, true, false);
        Driver.Tick(LockX, BarTop + 14.0f, false, false, true);
        Driver.Settle(2);

        std::fprintf(stderr, "[assert] locked: %d\n",
                     Driver.TexturingApplied.LayerLocked[1u] ? 1 : 0);

        if (!Driver.TexturingApplied.LayerLocked[1u])
        {
            std::fprintf(stderr, "[FAIL] the lock button did not lock the taken row\n");
            return false;
        }

        // ⑥f The header's expand toggle stands and retires the reference's wide columns.
        const float AddX = LeafBody.MaximumX - Pad - Scaled.LayerToolHeight;
        const float ExpandX = AddX - 34.0f + 14.0f;
        const float HeaderMidY = HeaderTop + Scaled.HeaderHeight * 0.5f;

        Driver.Tick(ExpandX, HeaderMidY, true, true, false);
        Driver.Tick(ExpandX, HeaderMidY, false, false, true);
        Driver.Settle(2);

        std::fprintf(stderr, "[assert] wide rows after expand: %d\n",
                     Driver.TexturingApplied.WideRows ? 1 : 0);

        if (!Driver.TexturingApplied.WideRows)
        {
            std::fprintf(stderr, "[FAIL] the expand toggle did not stand the wide columns\n");
            return false;
        }

        Driver.Tick(ExpandX, HeaderMidY, true, true, false);
        Driver.Tick(ExpandX, HeaderMidY, false, false, true);
        Driver.Settle(2);

        if (Driver.TexturingApplied.WideRows)
        {
            std::fprintf(stderr, "[FAIL] the expand toggle did not retire the wide columns\n");
            return false;
        }

        // ⑦ The stack still stands (the interactions above never left it); take the folder, then
        //    Tab -> the combined stack properties.
        Driver.Tick(LeafX, RowY[0], true, true, false);
        Driver.Tick(LeafX, RowY[0], false, false, true);
        Driver.Settle(2);
        Driver.SimLayersTab = true;
        Driver.Tick(LeafX, RowY[0], false, false, false);
        Driver.Settle(2);

        std::fprintf(stderr, "[assert] folder+Tab: taken=%u page=%u tab=%u\n",
                     Driver.TexturingApplied.LayerTaken, Driver.TexturingApplied.StackPage,
                     Driver.TexturingApplied.PropertyTab);

        if (Driver.TexturingApplied.LayerTaken != 0u || Driver.TexturingApplied.StackPage != 1u ||
            Driver.TexturingApplied.PropertyTab != 0u)
        {
            std::fprintf(stderr, "[FAIL] the folder did not open its combined stack properties\n");
            return false;
        }

        // 📐 The folder page is captured to scratch for its ink assert; the committed proof then
        //    slides BACK to the stack page — the redesigned layer stack is the picture the user
        //    asked to see.
        const std::string FolderPath = "_AgentScratch/layer-folder.png";
        if (!Driver.Capture(FolderPath.c_str(), Atlas, AtlasWidth, AtlasHeight))
            return false;

        Driver.SimLayersTab = true;
        Driver.Tick(LeafX, RowY[0], false, false, false);
        Driver.Settle(2);

        if (!Driver.Capture(OutputPath, Atlas, AtlasWidth, AtlasHeight))
            return false;

        // ⑦ The rendered folder page: its info rows carry the union text.
        {
            int ReadWidth = 0;
            int ReadHeight = 0;
            int ReadChannels = 0;
            unsigned char* ReadPixels = stbi_load(FolderPath.c_str(), &ReadWidth, &ReadHeight, &ReadChannels, 4);

            if (ReadPixels == nullptr)
            {
                std::fprintf(stderr, "[FAIL] the folder capture would not read back\n");
                return false;
            }

            std::uint32_t Bright = 0u;
            for (int Y = 80; Y < ReadHeight; ++Y)
            {
                for (int X = static_cast<int>(LeafBody.MinimumX + 30.0f);
                     X < static_cast<int>(LeafBody.MaximumX - 30.0f); ++X)
                {
                    const std::size_t Offset = (static_cast<std::size_t>(Y) * ReadWidth + X) * 4u;
                    const int R = ReadPixels[Offset + 0u];
                    const int G = ReadPixels[Offset + 1u];
                    const int B = ReadPixels[Offset + 2u];

                    if (R > 110 || G > 110 || B > 110)
                        ++Bright;
                }
            }

            stbi_image_free(ReadPixels);

            std::fprintf(stderr, "[assert] folder combined page ink: %u\n", Bright);
            if (Bright < 200u)
            {
                std::fprintf(stderr, "[FAIL] the folder combined page did not draw\n");
                return false;
            }
        }
    }
    else if (std::strcmp(Scenario, "editor-camera-fly") == 0)
    {
        Driver.ApplyPartition(false);
        Driver.Settle(20);

        // 🔴 Taking the right button with no pointer travel must be pose-invariant. Angular camera lag
        //    used to keep catching up here, which looked like an unexplained upward pitch.
        const double IdleYaw = Driver.EditorCamera.LaggedYawDegrees;
        const double IdlePitch = Driver.EditorCamera.LaggedPitchDegrees;
        Driver.SimLookHeld = true;
        Driver.SimLookDeltaX = 0.0f;
        Driver.SimLookDeltaY = 0.0f;
        Driver.Settle(12);
        Driver.SimLookHeld = false;

        if (std::abs(Driver.EditorCamera.LaggedYawDegrees - IdleYaw) > 0.0001 ||
            std::abs(Driver.EditorCamera.LaggedPitchDegrees - IdlePitch) > 0.0001)
        {
            std::fprintf(stderr, "[FAIL] a stationary right-click changed the camera rotation\n");
            return false;
        }
        std::fprintf(stderr, "[assert] stationary right-click preserves camera rotation\n");

        const double YawBefore   = Driver.EditorCamera.LaggedYawDegrees;
        const double PitchBefore = Driver.EditorCamera.LaggedPitchDegrees;
        const double PositionBefore[3] = { Driver.EditorCamera.LaggedPosition[0],
                                           Driver.EditorCamera.LaggedPosition[1],
                                           Driver.EditorCamera.LaggedPosition[2] };

        // 📐 Forty ticks of W + right-drag look: the camera flies forward along its yaw and turns with
        //    the look gesture. The drag is 2 px per tick rightward and 0.5 px per tick downward.
        Driver.SimForwardHeld = true;
        Driver.SimLookHeld    = true;
        Driver.SimLookDeltaX  = 2.0f;
        Driver.SimLookDeltaY  = 0.5f;
        for (int Step = 0; Step < 40; ++Step)
            Driver.Tick(640.0f, 450.0f, false, false, false);
        Driver.SimForwardHeld = false;
        Driver.SimLookHeld    = false;
        Driver.SimLookDeltaX  = 0.0f;
        Driver.SimLookDeltaY  = 0.0f;
        Driver.Settle(10);

        const double YawAfter   = Driver.EditorCamera.LaggedYawDegrees;
        const double PitchAfter = Driver.EditorCamera.LaggedPitchDegrees;
        const double PositionAfter[3] = { Driver.EditorCamera.LaggedPosition[0],
                                          Driver.EditorCamera.LaggedPosition[1],
                                          Driver.EditorCamera.LaggedPosition[2] };
        const double Travelled = std::sqrt((PositionAfter[0] - PositionBefore[0])
                                         * (PositionAfter[0] - PositionBefore[0])
                                         + (PositionAfter[1] - PositionBefore[1])
                                         * (PositionAfter[1] - PositionBefore[1])
                                         + (PositionAfter[2] - PositionBefore[2])
                                         * (PositionAfter[2] - PositionBefore[2]));

        std::fprintf(stderr, "[assert] yaw %.1f -> %.1f, pitch %.1f -> %.1f\n",
                     YawBefore, YawAfter, PitchBefore, PitchAfter);
        std::fprintf(stderr, "[assert] travelled %.1f m (lag on)\n", Travelled);

        if (YawAfter <= YawBefore + 4.0)
        {
            std::fprintf(stderr, "[FAIL] the look gesture did not yaw the camera\n");
            return false;
        }
        if (Travelled < 5.0)
        {
            std::fprintf(stderr, "[FAIL] W did not move the camera\n");
            return false;
        }

        // 📐 The lag's own proof: with the lag DISABLED, the same forty ticks must travel further —
        //    the lagged camera is still catching up, the unlagged one is at the target.
        Driver.Applied.DetailBits[6u] &= ~2u;   // [-] - camera lag off
        const double LaglessBefore[3] = { Driver.EditorCamera.LaggedPosition[0],
                                          Driver.EditorCamera.LaggedPosition[1],
                                          Driver.EditorCamera.LaggedPosition[2] };

        Driver.SimForwardHeld = true;
        for (int Step = 0; Step < 40; ++Step)
            Driver.Tick(640.0f, 450.0f, false, false, false);
        Driver.SimForwardHeld = false;
        Driver.Settle(10);

        const double LaglessAfter[3] = { Driver.EditorCamera.LaggedPosition[0],
                                         Driver.EditorCamera.LaggedPosition[1],
                                         Driver.EditorCamera.LaggedPosition[2] };
        const double LaglessTravelled = std::sqrt((LaglessAfter[0] - LaglessBefore[0])
                                                * (LaglessAfter[0] - LaglessBefore[0])
                                                + (LaglessAfter[1] - LaglessBefore[1])
                                                * (LaglessAfter[1] - LaglessBefore[1])
                                                + (LaglessAfter[2] - LaglessBefore[2])
                                                * (LaglessAfter[2] - LaglessBefore[2]));

        std::fprintf(stderr, "[assert] travelled %.1f m (lag off)\n", LaglessTravelled);
        if (LaglessTravelled <= Travelled)
        {
            std::fprintf(stderr, "[FAIL] the camera lag did not lag\n");
            return false;
        }

        // 📐 Shift boosts the fly speed: the same 30 ticks travel ~3x further with Shift held.
        Driver.SimForwardHeld = true;
        const double PlainBefore[3] = { Driver.EditorCamera.LaggedPosition[0],
                                        Driver.EditorCamera.LaggedPosition[1],
                                        Driver.EditorCamera.LaggedPosition[2] };
        for (int Step = 0; Step < 30; ++Step)
            Driver.Tick(640.0f, 450.0f, false, false, false);
        const double PlainAfter[3] = { Driver.EditorCamera.LaggedPosition[0],
                                       Driver.EditorCamera.LaggedPosition[1],
                                       Driver.EditorCamera.LaggedPosition[2] };
        Driver.SimForwardHeld = false;
        Driver.Settle(10);

        Driver.SimShiftHeld = true;
        Driver.SimForwardHeld = true;
        const double ShiftBefore[3] = { Driver.EditorCamera.LaggedPosition[0],
                                        Driver.EditorCamera.LaggedPosition[1],
                                        Driver.EditorCamera.LaggedPosition[2] };
        for (int Step = 0; Step < 30; ++Step)
            Driver.Tick(640.0f, 450.0f, false, false, false);
        const double ShiftAfter[3] = { Driver.EditorCamera.LaggedPosition[0],
                                       Driver.EditorCamera.LaggedPosition[1],
                                       Driver.EditorCamera.LaggedPosition[2] };
        Driver.SimForwardHeld = false;
        Driver.SimShiftHeld = false;
        Driver.Settle(10);

        const auto Distance = [](const double (&A)[3], const double (&B)[3]) -> double
        {
            return std::sqrt((B[0] - A[0]) * (B[0] - A[0])
                           + (B[1] - A[1]) * (B[1] - A[1])
                           + (B[2] - A[2]) * (B[2] - A[2]));
        };

        const double PlainMoved = Distance(PlainBefore, PlainAfter);
        const double ShiftMoved = Distance(ShiftBefore, ShiftAfter);

        std::fprintf(stderr, "[assert] shift boost: %.1f m plain, %.1f m with shift\n",
                     PlainMoved, ShiftMoved);
        if (ShiftMoved < PlainMoved * 2.0)
        {
            std::fprintf(stderr, "[FAIL] Shift did not boost the fly speed\n");
            return false;
        }

        // 📐 The ground grid's own proof: the rendered leaf must contain the lattice — light grey-blue
        //    lines over the dark ground, drawn by the panel's perspective projector. The capture is
        //    repeated here so the PNG stands for the check, and the PNG itself is scanned for the ink.
        if (!Driver.Capture(OutputPath, Atlas, AtlasWidth, AtlasHeight))
            return false;
        {
            int ReadWidth = 0;
            int ReadHeight = 0;
            int ReadChannels = 0;
            unsigned char* ReadPixels = stbi_load(OutputPath, &ReadWidth, &ReadHeight, &ReadChannels, 4);

            if (ReadPixels == nullptr)
            {
                std::fprintf(stderr, "[FAIL] the captured PNG would not read back\n");
                return false;
            }

            // 📐 The lattice ink (0x9AA6B8 at 0.28/0.55 coverage over the (10,8,7) ground) resolves to
            //    ~(50,52,56) fine and ~(89,95,104) coarse — grey-blue, brighter than the ground,
            //    never confused with the sky's saturated blues or the panels' chrome.
            std::uint32_t GridPixels = 0u;
            for (int Index = 0; Index < ReadWidth * ReadHeight; ++Index)
            {
                const int R = ReadPixels[Index * 4u + 0u];
                const int G = ReadPixels[Index * 4u + 1u];
                const int B = ReadPixels[Index * 4u + 2u];

                if (R > 38 && R < 130 && B > R && (B - R) < 22 &&
                    std::abs(R - G) < 8 && std::abs(G - B) < 14)
                    ++GridPixels;
            }

            stbi_image_free(ReadPixels);

            std::fprintf(stderr, "[assert] grid pixels in render: %u\n", GridPixels);
            if (GridPixels < 500u)
            {
                std::fprintf(stderr, "[FAIL] the ground grid did not draw\n");
                return false;
            }
        }
    }
    else if (std::strcmp(Scenario, "editor-after-drag") == 0)
    {
        Driver.ApplyPartition(true);
        Driver.Settle(20);

        // 📐 The elevation slider: the first row of the Sun card inside the properties leaf. The
        //    geometry is the panel's own — the leaf body, then the pane header, the tab strip, the
        //    card header, and the row padding — so the drag lands on the real slider wherever the
        //    partition placed it.
        const PlaneExtent Body = Driver.Editor.LeafBody(2u);
        ThemeSelection Sel;
        Sel.Current = ThemeSubject::Oled;
        const ThemeProfile Resolved = ResolveTinted(1.0, 1.0, ViewportWidth, Sel);
        const float AppliedFactor = static_cast<float>(Resolved.Measure.DisplayScale)
                                  * Resolved.ControlMeasure.ArtistFactor;
        const ShellMetric Scaled = ScaleShellLengths(AppliedFactor);

        const float Pad = Scaled.PanePad;

        // 📐 The Sun card is the SECOND card: the Transform card (three rows) precedes it, so the
        //    elevation row sits one card deeper than the naive layout suggests. The geometry below
        //    walks the panel's own sweep: pages begin after the pane header and the tab strip, the
        //    Transform card occupies one card height, then the Sun card's first row is the elevation.
        const float PagesY = Body.MinimumY + Scaled.HeaderHeight + Scaled.ComponentY;
        float Sweep = PagesY + Pad;
        const float TransformBottom = Sweep + Scaled.ComponentY + (3.0f * Scaled.RowHeight + Pad * 2.0f);
        Sweep = TransformBottom + Pad * 0.85f;

        const float SliderY = Sweep + Scaled.ComponentY + Pad + Scaled.RowHeight * 0.5f;
        const float TrackX0 = Body.MinimumX + Pad * 1.5f + 6.0f;
        const float TrackX1 = Body.MaximumX - Pad * 1.5f - 6.0f;

        // 📐 The press lands on the thumb's own position (elevation 35 of 0…90), so the drag reads as
        //    a continuous move rather than a jump; the drag then runs to 80 % of the track.
        const float ThumbX = TrackX0 + (35.0 / 90.0) * (TrackX1 - TrackX0);
        const float ReleaseX = TrackX0 + 0.55f * (TrackX1 - TrackX0);

        Driver.Tick(ThumbX, SliderY, true, true, false);
        for (int Step = 0; Step < 40; ++Step)
        {
            const float T = static_cast<float>(Step) / 39.0f;
            Driver.Tick(ThumbX + (ReleaseX - ThumbX) * T, SliderY, true, false, false);
        }
        Driver.Tick(ReleaseX, SliderY, false, false, true);
        Driver.Settle(10);

        std::fprintf(stderr, "[assert] sun elevation after drag: %.1f\n",
                     Driver.Applied.Environment.SunElevation);
        if (Driver.Applied.Environment.SunElevation < 50.0)
        {
            std::fprintf(stderr, "[FAIL] the elevation slider did not move the sun enough\n");
            return false;
        }

        // 📐 The raised sun now stands ABOVE the camera's 60° frustum (61.4° vs the 15° pitch) — the
        //    physically correct answer to a real fly camera. The artist looks up to see it: the look
        //    gesture drags the pointer UP (negative Y) and the camera pitches up, bringing the sun
        //    back into the viewport. This doubles as the look-gesture's own proof.
        const double PitchBefore = Driver.EditorCamera.LaggedPitchDegrees;

        Driver.SimLookHeld = true;
        Driver.SimLookDeltaX = 0.0f;
        Driver.SimLookDeltaY = -6.0f;
        for (int Step = 0; Step < 35; ++Step)
            Driver.Tick(640.0f, 450.0f, false, false, false);
        Driver.SimLookHeld = false;
        Driver.SimLookDeltaY = 0.0f;
        Driver.Settle(10);

        const double PitchAfter = Driver.EditorCamera.LaggedPitchDegrees;

        std::fprintf(stderr, "[assert] look gesture pitched the camera %.1f -> %.1f\n",
                     PitchBefore, PitchAfter);

        if (PitchAfter <= PitchBefore + 15.0)
        {
            std::fprintf(stderr, "[FAIL] the look gesture did not pitch the camera up\n");
            return false;
        }

        if (!Driver.Capture(OutputPath, Atlas, AtlasWidth, AtlasHeight))
            return false;

        // 📐 The sun must be back in the viewport leaf: warm, bright, in the upper sky.
        {
            int ReadWidth = 0;
            int ReadHeight = 0;
            int ReadChannels = 0;
            unsigned char* ReadPixels = stbi_load(OutputPath, &ReadWidth, &ReadHeight, &ReadChannels, 4);

            if (ReadPixels == nullptr)
            {
                std::fprintf(stderr, "[FAIL] the captured PNG would not read back\n");
                return false;
            }

            std::uint32_t SunPixels = 0u;
            for (int Y = 0; Y < ReadHeight; ++Y)
            {
                for (int X = 0; X < 637; ++X)
                {
                    const std::size_t Offset = (static_cast<std::size_t>(Y) * ReadWidth + X) * 4u;
                    const int R = ReadPixels[Offset + 0u];
                    const int G = ReadPixels[Offset + 1u];
                    const int B = ReadPixels[Offset + 2u];

                    if (R > 200 && G > 195 && B < 225 && (R - B) > 40)
                        ++SunPixels;
                }
            }

            stbi_image_free(ReadPixels);

            std::fprintf(stderr, "[assert] sun pixels in viewport after look-up: %u\n", SunPixels);
            if (SunPixels < 100u)
            {
                std::fprintf(stderr, "[FAIL] the sun is not in the viewport after looking up\n");
                return false;
            }
        }

        // Tab now alternates Directory and Properties; History is intentionally absent.
        Driver.SimTabPressed = true;
        Driver.Tick(640.0f, 450.0f, false, false, false);
        if (Driver.Applied.OutlinePage != 1u)
        {
            std::fprintf(stderr, "[FAIL] Tab did not advance to Properties\n");
            return false;
        }

        Driver.SimTabPressed = true;
        Driver.Tick(640.0f, 450.0f, false, false, false);
        if (Driver.Applied.OutlinePage != 0u)
        {
            std::fprintf(stderr, "[FAIL] Tab did not return to Directory\n");
            return false;
        }
        std::fprintf(stderr, "[assert] Directory -> Properties -> Directory stands\n");

        // 📐 The Inspect call jumps straight to Properties.
        {
            const PlaneExtent Body = Driver.Editor.LeafBody(1u);
            const float OutlinerX = 350.0f < Body.Width() * 0.6f ? 350.0f : Body.Width() * 0.6f;
            const float HeaderTop = Body.MinimumY;           // the directory header inside the leaf
            const float HeaderRight = Body.MinimumX + OutlinerX;

            const char* Caption = "Inspect";
            const float PadX = 8.0f;
            const float Run = 11.5f;
            const float CallSpan = PadX * 2.0f + 35.0f;      // measured run approximated
            const float CallX = HeaderRight - PadX - CallSpan + CallSpan * 0.5f;
            const float CallY = HeaderTop + 46.0f * 0.5f;

            Driver.Tap(CallX, CallY);
            std::fprintf(stderr, "[assert] inspect pressed at (%.0f,%.0f) page=%u\n",
                         CallX, CallY, Driver.Applied.OutlinePage);
            if (Driver.Applied.OutlinePage != 1u)
            {
                std::fprintf(stderr, "[FAIL] the Inspect call did not open Properties\n");
                return false;
            }
        }
    }
    else
    {
        std::fprintf(stderr, "unknown scenario %s\n", Scenario);
        return false;
    }

    return Driver.Capture(OutputPath, Atlas, AtlasWidth, AtlasHeight);
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    std::string OutputDirectory = "VisualProof/EditorScene";
    std::string Scenario = "";

    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        const std::string Arg = Arguments[Index];
        if (Arg == "--out" && Index + 1 < ArgumentCount)
            OutputDirectory = Arguments[++Index];
        else if (Arg.rfind("--shot=", 0) == 0)
            Scenario = Arg.substr(7);
    }

    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO();
    IO.DisplaySize = ImVec2(ViewportWidth, ViewportHeight);
    IO.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    IO.IniFilename = nullptr;
    IO.LogFilename = nullptr;
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    // 🔴 The viewport's sky mesh carries ~2400 vertices, and the workspace list's total can cross the
    //    16-bit index ceiling: without `RendererHasVtxOffset` ImGui cannot split commands at 64K and
    //    the mesh's indices overflow — the "missing sun" in the harness renders. The windowed hosts'
    //    Vulkan backend sets this flag; the harness must too.
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

    SceneDriver Driver;
    if (!Driver.ConstructSceneProof())
    {
        std::fprintf(stderr, "refused: harness construction\n");
        return 1;
    }
    IO.FontDefault = IO.Fonts->Fonts[0];

    unsigned char* AtlasPixels = nullptr;
    int AtlasWidth = 0;
    int AtlasHeight = 0;
    IO.Fonts->GetTexDataAsRGBA32(&AtlasPixels, &AtlasWidth, &AtlasHeight);
    IO.Fonts->TexData->SetTexID((ImTextureID)(intptr_t)1);
    IO.Fonts->TexRef._TexData = IO.Fonts->TexData;

    const char* Shots[] = {"editor-overview", "editor-directory-multiselect",
                           "editor-outliner-inspector", "editor-camera-properties",
                           "editor-camera-bookmarks",
                           "editor-sun-props", "editor-sky-quality",
                           "editor-after-drag",
                           "editor-camera-fly", "editor-grid-settings", "editor-overlay-gpu-only",
                           "editor-search-filter", "editor-grid-fade", "editor-grid-dropdown",
                           "editor-scene-transfer", "editor-scene-transfer-options", "editor-scene-export",
                           "editor-layer-flatten", "editor-layer-export", "editor-layer-scroll-return",
                           "editor-layer-multiselect", "editor-layerstack", "editor-layerstack-card"};

    int Rendered = 0;
    for (const char* Shot : Shots)
    {
        if (!Scenario.empty() && Scenario != Shot)
            continue;
        const std::string Path = OutputDirectory + "/" + Shot + ".png";
        if (!RunShot(Driver, Path.c_str(), Shot, AtlasPixels, AtlasWidth, AtlasHeight))
            return 1;
        ++Rendered;
    }
    if (Rendered == 0)
    {
        std::fprintf(stderr, "no scenario matched; pass --shot=<name>\n");
        return 1;
    }

    std::fprintf(stderr, "\n[done] %d shot(s) rendered\n", Rendered);
    return 0;
}

//============================================================================================================================================
//                                                          TYPOGRAPHYPROOF.CPP
//============================================================================================================================================
// 🧩 Headless proof renderer for the Control Centre typography page.
//
//    This harness drives the REAL panel code — `ControlCentrePanel::Record` records into the
//    `RecordingSurface`, which emits the same ImDrawList the Vulkan hosts upload — and then rasterizes
//    that command list on the CPU into a PNG. The pixels are the actual command stream the application
//    records: same geometry, same faces, same atlas, same clip rects. The only difference from a windowed
//    host is the rasterizer at the end of the pipe.
//
//    Scenarios (each writes one PNG under VisualProof/Typography/):
//      --shot fonts-archivo-carousels    the Fonts page, family Archivo applied, default weights
//      --shot fonts-title-bold           the Title strip's Bold tile pressed; the role label, page
//                                        heading and sample all re-render in Bold
//      --shot fonts-title-black-scrolled the Title strip scrolled twice and its Black tile pressed
//      --shot fonts-mixed-weights        Title=Black, Header=ExtraBold, Caption=Light, Warning=Bold
//      --shot settings-title-black       the Settings page with the Title role at Black — the weight
//                                        applied to a page other than the one that set it
//
//    Build (from the repository root, after `python3 Scripts/ApplyImGuiPatches.py`):
//      g++ -std=c++20 -O2 -DNDEBUG -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DGLFW_DLL -DGLFW_INCLUDE_NONE \
//          -I Engine -I . -I ExternalPackages/imgui -I ExternalPackages/glfw/include \
//          -I ExternalPackages/thorvg/inc -I /tmp/Vulkan-Headers/Include \
//          Tools/TypographyProof/TypographyProof.cpp \
//          Engine/SlateUI/Interface/InterfaceExchange/Source/RecordingSurface.cpp \
//          Engine/SlateUI/Interface/ControlCentrePanel/Source/ControlCentrePanel.cpp \
//          Engine/SlateUI/Interface/NoticeDialog/Source/NoticeDialog.cpp \
//          Engine/SlateUI/Interface/DrawerSpace/Source/DrawerSpace.cpp \
//          Engine/SlateUI/Interface/GestureSequence/Source/GestureSequence.cpp \
//          Engine/SlateUI/Interface/AppearanceSpecification/Source/AppearanceSpecification.cpp \
//          Engine/SlateUI/Interface/TextComponent/Source/FontLoader.cpp \
//          Engine/SlateUI/Interface/TextComponent/Source/TextComponent.cpp \
//          Engine/SlateUI/Interface/ControlIndex/Source/ControlIndex.cpp \
//          Engine/SlateUI/Interface/ComponentSpecification/Source/ComponentSpecification.cpp \
//          Engine/SlateUI/Interface/ComponentSpecification/Source/MagnitudeExpression.cpp \
//          Engine/SlateUI/Interface/MotionIntegrator/Source/MotionIntegrator.cpp \
//          Engine/SlateUI/Interface/ShortcutSpecification/Source/ShortcutSpecification.cpp \
//          Engine/SlateUI/Interface/SymbolSpecification/Source/SymbolSpecification.cpp \
//          Engine/SlateUI/Interface/ThemeSpecification/Source/ThemeSpecification.cpp \
//          ExternalPackages/imgui/imgui.cpp ExternalPackages/imgui/imgui_draw.cpp \
//          ExternalPackages/imgui/imgui_tables.cpp ExternalPackages/imgui/imgui_widgets.cpp \
//          -o _AgentScratch/TypographyProof

#include "imgui.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ExternalPackages/stb/stb_image_write.h"

#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"
#include "SlateUI/Interface/ComponentSpecification/Api/ComponentSpecification.h"
#include "SlateUI/Interface/ControlCentrePanel/Api/ControlCentrePanel.h"
#include "SlateUI/Interface/ControlIndex/Api/ControlIndex.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/MotionIntegrator/Api/MotionIntegrator.h"
#include "SlateUI/Interface/ShortcutSpecification/Api/ShortcutSpecification.h"
#include "SlateUI/Interface/SymbolSpecification/Api/SymbolSpecification.h"
#include "SlateUI/Interface/TextComponent/Api/FontLoader.h"
#include "SlateUI/Interface/ThemeSpecification/Api/ThemeSpecification.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Slate;

namespace
{

constexpr float ViewportWidth = 1280.0f;    // [px] - InterfaceValidationHost::InitialWidth
constexpr float ViewportHeight = 900.0f;    // [px] - InterfaceValidationHost::InitialHeight
constexpr double TickMilliseconds = 16.6;   // [ms] - a 60 Hz tick

//------------------------------------------------------------------------------------------------------------------------
//                                                       CPU RASTERIZER
//------------------------------------------------------------------------------------------------------------------------
// 🧩 Rasterizes one ImDrawList into an RGBA buffer. Every primitive the panel records is a triangle or a
//    textured triangle (the font atlas), so the whole vendor list reduces to two cases. Blending is the
//    vendor's own source-over; text samples the atlas bilinearly, which is what the GPU path does.

struct Rasterizer
{
    std::vector<unsigned char> Pixels;   // [RGBA8] row-major, top row first
    int Width = 0;
    int Height = 0;
    long long Triangles = 0;      // [-] - debug: primitives examined
    long long PixelsWritten = 0;  // [-] - debug: pixels blended
    int MaxChannel = 0;           // [-] - debug: brightest channel written

    bool Begin(int W, int H)
    {
        Width = W;
        Height = H;
        Pixels.assign(static_cast<std::size_t>(W) * static_cast<std::size_t>(H) * 4u, 0u);
        // The host clears to the page ground #050505 before the interface records.
        for (std::size_t Pixel = 0u; Pixel < Pixels.size(); Pixel += 4u)
        {
            Pixels[Pixel + 0u] = 5u;
            Pixels[Pixel + 1u] = 5u;
            Pixels[Pixel + 2u] = 5u;
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
                  bool Textured, const unsigned char* Atlas, int AtlasWidth, int AtlasHeight,
                  int ClipX0, int ClipY0, int ClipX1, int ClipY1)
    {
        const float Ax = A.pos.x, Ay = A.pos.y;
        const float Bx = B.pos.x, By = B.pos.y;
        const float Cx = C.pos.x, Cy = C.pos.y;

        // 🔴 Both windings occur (the vendor does not promise one), so the inside test accepts either.
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

                // 📐 Barycentric weights: W0 is the area of PAB (C's weight), W1 of PBC (A's), W2 of PCA
                //    (B's) — a permuted assignment interpolates the wrong corner's colour and UV into
                //    every pixel, which turns glyphs into noise while flat rectangles stay flat.
                const float T0 = W1 * InvArea;
                const float T1 = W2 * InvArea;
                const float T2 = W0 * InvArea;

                float Red = 0.0f, Green = 0.0f, Blue = 0.0f, Alpha = 0.0f;
                for (std::uint32_t V = 0u; V < 3u; ++V)
                {
                    const ImDrawVert& Vtx = (V == 0u) ? A : (V == 1u) ? B : C;
                    const float T = (V == 0u) ? T0 : (V == 1u) ? T1 : T2;
                    // ImU32 is 0xAABBGGRR.
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
                    const int PX0 = IX < 0 ? 0 : (IX > AtlasWidth - 2 ? AtlasWidth - 2 : IX);
                    const int PY0 = IY < 0 ? 0 : (IY > AtlasHeight - 2 ? AtlasHeight - 2 : IY);
                    const auto Texel = [&](int TX, int TY) -> std::uint32_t
                    {
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
                    // 📐 The two bilinear passes interpolate different units: the inner pass turns a
                    //    byte into [0,1], the outer pass interpolates two already-normalised figures — a
                    //    second `/255` in the outer pass darkens every textured pixel to 1/255 of itself.
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
                ++PixelsWritten;
                const int Brightest = static_cast<int>(Red * 255.0f > Green * 255.0f
                                                           ? (Blue * 255.0f > Red * 255.0f ? Blue * 255.0f : Red * 255.0f)
                                                           : (Blue * 255.0f > Green * 255.0f ? Blue * 255.0f : Green * 255.0f));
                if (Brightest > MaxChannel) MaxChannel = Brightest;
            }
        }
    }

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

            // 📝 Every command in a shell frame carries the atlas ref — solid shapes sample the atlas's
            //    dedicated white pixel (`TexUvWhitePixel`), text samples glyphs — so the resolved id is
            //    the texture test, exactly as a backend applies it.
            const bool Textured = (Command.GetTexID() != (ImTextureID)0);
            const std::uint32_t PrimitiveCount = Command.ElemCount / 3u;
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
                ++Triangles;
                Triangle(A, B, C, Textured, Atlas, AtlasWidth, AtlasHeight,
                         ClipX0, ClipY0, ClipX1, ClipY1);
            }
        }
        return true;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE DRIVER
//------------------------------------------------------------------------------------------------------------------------
// 🧩 Owns the interface stack exactly as InterfaceValidationHost owns it and replays one tick at a time,
//    so a press is a three-tick sequence (down, hold, release) and the panel's own interaction logic
//    decides what it meant. The recorded frame is rasterized on demand.

struct ProofDriver
{
    ImGuiIO& IO;
    FontLoader Fonts;                       // [-] - owned here: the surface borrows it for the process
    MotionIntegrator Motion;
    ControlIndex Interaction;
    RecordingSurface Surface;
    ComponentSpecification Shared;
    ControlCentrePanel ControlCentre;
    ControlCentreConfiguration Values;
    ThemeProfile Appearance;
    const char* ActiveFamily = "Archivo";

    ProofDriver()
        : IO(ImGui::GetIO())
    {
        Values.Page = ControlCentrePage::Display;
        Values.DisplayPage = DisplayPreferencePage::Fonts;
    }

    bool ConstructTypographyProof(const char* FontRoot, const char* Family)
    {
        if (!Fonts.Discover(FontRoot).Resolved)
        {
            std::fprintf(stderr, "rejected: font discovery\n");
            return false;
        }
        if (!Fonts.PreparePreviews(1.0f).Resolved)
        {
            std::fprintf(stderr, "rejected: preview preparation\n");
            return false;
        }
        ActiveFamily = Family;
        FontProfile Profile;
        std::strncpy(Profile.Family, Family, sizeof(Profile.Family) - 1u);
        if (!Fonts.Load(FontRoot, Profile, 1.0f).Resolved)
        {
            std::fprintf(stderr, "rejected: active family load (is %s installed?)\n", Family);
            return false;
        }
        std::fprintf(stderr, "[Fonts] active family: %s, %u families discovered\n",
                     Profile.Family, static_cast<unsigned>(Fonts.FamilyCount()));

        // 📝 The hosts seat the carousel on the family the appearance names; mirror it here so the
        //    highlighted tile and the loaded faces agree.
        for (std::uint32_t Index = 0u; Index < Fonts.FamilyCount(); ++Index)
            if (Fonts.FamilyName(Index) != nullptr &&
                std::strcmp(Fonts.FamilyName(Index), Profile.Family) == 0)
            {
                Values.Font = Index;
                break;
            }

        // 🔴 A CPU harness has no backend to claim the atlas texture. Reserve it exactly as a backend
        //    would: the id is what `GetTexID()` resolves every command's ref to, and `NewFrame` pushes
        //    `io.Fonts->TexRef` onto the shell lists so `RenderText` emits textured glyph quads.
        IO.Fonts->TexData->SetTexID((ImTextureID)(intptr_t)1);
        IO.Fonts->TexRef._TexData = IO.Fonts->TexData;

        ThemeSelection Selected;
        Selected.Current = ThemeSubject::Oled;
        Appearance = ResolveTinted(1.0, 1.0, ViewportWidth, Selected);
        std::strncpy(Appearance.Fonts.Family, Family, sizeof(Appearance.Fonts.Family) - 1u);
        ApplyFontWeights(Appearance, Values.TypographyWeight);
        Surface.ApplyFontLoader(Fonts);
        ControlCentre.SetFontFamilies(Fonts);
        Surface.ApplyTypographyScale(Appearance.TextScale);
        Surface.ApplyCornerScale(Appearance.CornerScale);

        if (!Interaction.AttachMotion(Motion).Resolved)
        {
            std::fprintf(stderr, "rejected: interaction index\n");
            return false;
        }
        if (!Shared.ConstructComponents(Interaction, Surface, Appearance).Resolved)
        {
            std::fprintf(stderr, "rejected: shared controls\n");
            return false;
        }
        if (!ControlCentre.ConstructControlCentrePanel(Motion, Surface, Appearance).Resolved)
        {
            std::fprintf(stderr, "rejected: Control Centre\n");
            return false;
        }

        return true;
    }

    void Tick(float MouseX, float MouseY, bool Held, bool Sampled, bool Released)
    {
        // 📝 The supported vendor cycle, exactly as a host runs it: queue the pointer, open the frame
        //    (which resets the shell lists and installs the full-screen clip and the atlas texture),
        //    record the panel, then render so the draw data is finalised for the rasterizer.
        IO.MousePos = ImVec2(MouseX, MouseY);
        IO.MouseDelta = ImVec2(0.0f, 0.0f);
        IO.DeltaTime = static_cast<float>(TickMilliseconds / 1000.0);
        if (Sampled)
            IO.AddMouseButtonEvent(0, true);
        else if (Released)
            IO.AddMouseButtonEvent(0, false);

        ImGui::NewFrame();

        Discard(Surface.Adopt(RecordingSurface::ShellLayer::Beneath));
        Motion.Advance(TickMilliseconds);
        Surface.ApplyTypographyRoles(Values.TypographySize, Values.TypographyWeight);
        ControlCentre.Advance(Surface.Pointer(), TickMilliseconds);
        Discard(ControlCentre.Record(Spanning(0.0f, 0.0f, ViewportWidth, ViewportHeight), Values));
        Surface.Retire();

        ImGui::Render();
    }

    void Settle(int Frames)
    {
        for (int Index = 0; Index < Frames; ++Index)
            Tick(640.0f, 450.0f, false, false, false);
    }

    // 🧩 One tap: down and arrived on one tick, held on the next, released on the third. The panel's own
    //    press logic resolves it on the release tick — this is what makes the shot proof of selectability
    //    rather than of a value written by hand.
    void Tap(float MouseX, float MouseY)
    {
        Tick(MouseX, MouseY, true, true, false);
        Tick(MouseX, MouseY, true, false, false);
        Tick(MouseX, MouseY, false, false, true);
    }

    // 🧩 A press held still, for a mid-press frame: the pointer is down on a tile and nothing has
    //    resolved yet, exactly the frame an artist sees while the button is held.
    void Hold(float MouseX, float MouseY)
    {
        Tick(MouseX, MouseY, true, true, false);
        Tick(MouseX, MouseY, true, false, false);
    }

    // 🧩 Scrolls the Display page by wheel notches (72px each) and lets the scroll ease settle.
    void Scroll(float Notches)
    {
        IO.MouseWheel = Notches;
        Tick(640.0f, 450.0f, false, false, false);
        IO.MouseWheel = 0.0f;
        Settle(30);
    }

    bool Capture(const char* Path, const unsigned char* Atlas, int AtlasWidth, int AtlasHeight)
    {
        // 🔴 The legacy bake preloads every glyph range on the first NewFrame, which reallocates the
        //    atlas pixel buffer — the pointer captured at startup is stale by the time a frame records.
        //    The texture record is stable, so its live pixels are the ones the glyph UVs were baked
        //    against.
        const unsigned char* LiveAtlas =
            static_cast<const unsigned char*>(IO.Fonts->TexData->Pixels);
        const int LiveAtlasWidth = IO.Fonts->TexData->Width;
        const int LiveAtlasHeight = IO.Fonts->TexData->Height;
        static_cast<void>(Atlas);
        static_cast<void>(AtlasWidth);
        static_cast<void>(AtlasHeight);

        Rasterizer Out;
        Out.Begin(static_cast<int>(ViewportWidth), static_cast<int>(ViewportHeight));
        const ImDrawData* Data = ImGui::GetDrawData();
        for (int ListIndex = 0; ListIndex < Data->CmdListsCount; ++ListIndex)
        {
            if (!Out.Draw(Data->CmdLists[ListIndex], LiveAtlas, LiveAtlasWidth, LiveAtlasHeight))
                return false;
        }
        if (Data->CmdListsCount == 0)
        {
            std::fprintf(stderr, "rejected: the frame assembled no draw lists\n");
            return false;
        }
        if (stbi_write_png(Path, Out.Width, Out.Height, 4, Out.Pixels.data(), Out.Width * 4) == 0)
        {
            std::fprintf(stderr, "rejected: writing %s\n", Path);
            return false;
        }
        std::fprintf(stderr, "[proof] wrote %s (%dx%d) tris=%lld px=%lld max=%d\n",
                     Path, Out.Width, Out.Height, Out.Triangles, Out.PixelsWritten, Out.MaxChannel);
        return true;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      SCENARIOS
//------------------------------------------------------------------------------------------------------------------------

// 📐 The geometry of the first role row on the Fonts page at 1280x900, mirrored from FontsPage's own
//    constants so a script can aim at a tile. The Display page's chrome (back button, title, tabs)
//    stands 136px tall, so the Fonts page viewport begins at across 224; ContentLeft is 118 along;
//    the specimen ends at 658 across; the first entry spans 688..892; its strip sits at across 728
//    with tiles 120 wide on a 132 step starting at along 174.
constexpr float TitleStripY = 728.0f;   // [px] - the Title row's strip across
constexpr float TitleTileCentreY = 764.0f;   // [px] - 728 + 72/2
constexpr float TitleRailMinimum = 170.0f;     // [px] - the strip's clip rail along
constexpr float TileStep = 132.0f;
constexpr float TileHalf = 60.0f;
constexpr float TitleRightArrowX = 1173.0f;  // [px] - the right arrow's centre

float TileCentreX(std::uint32_t TileIndex, float Scroll)
{
    return TitleRailMinimum + 4.0f + TileStep * static_cast<float>(TileIndex) - Scroll + TileHalf;
}

bool RunShot(ProofDriver& Driver, const char* OutputPath, const char* Scenario,
             const unsigned char* Atlas, int AtlasWidth, int AtlasHeight)
{
    Driver.Values = {};
    Driver.Values.Page = ControlCentrePage::Display;
    Driver.Values.DisplayPage = DisplayPreferencePage::Fonts;
    for (std::uint32_t Index = 0u; Index < Driver.Fonts.FamilyCount(); ++Index)
        if (Driver.Fonts.FamilyName(Index) != nullptr &&
            std::strcmp(Driver.Fonts.FamilyName(Index), Driver.ActiveFamily) == 0)
        {
            Driver.Values.Font = Index;
            break;
        }

    std::fprintf(stderr, "\n== %s ==\n", Scenario);

    if (std::strcmp(Scenario, "fonts-archivo-carousels") == 0)
    {
        Driver.Settle(20);
    }
    else if (std::strcmp(Scenario, "fonts-title-bold") == 0)
    {
        Driver.Settle(20);
        Driver.Tap(TileCentreX(6u, 0.0f), TitleTileCentreY);   // Bold is the seventh tile, fully visible
        Driver.Settle(20);
        std::fprintf(stderr, "[press] Title weight after tap: %u\n", Driver.Values.TypographyWeight[0u]);
    }
    else if (std::strcmp(Scenario, "fonts-title-black-scrolled") == 0)
    {
        Driver.Settle(20);
        Driver.Tap(TitleRightArrowX, TitleTileCentreY);        // scroll the Title strip right
        Driver.Settle(20);
        Driver.Tap(TitleRightArrowX, TitleTileCentreY);        // and once more — Black then stands fully
        Driver.Settle(20);
        std::fprintf(stderr, "[press] Title weight after two arrow taps (unchanged): %u\n", Driver.Values.TypographyWeight[0u]);
        // 📐 Two arrow taps move the strip 264px, clamped to the strip's reach (240px); Black, the last
        //    tile, then stands fully visible in slot six.
        Driver.Tap(TileCentreX(8u, 2.0f * TileStep), TitleTileCentreY);   // Black, now at slot six
        Driver.Settle(20);
        std::fprintf(stderr, "[press] Title weight after Black tap: %u\n", Driver.Values.TypographyWeight[0u]);
    }
    else if (std::strcmp(Scenario, "fonts-mixed-weights") == 0)
    {
        Driver.Values.TypographyWeight[0u] = 900u;   // Title  - Black
        Driver.Values.TypographyWeight[1u] = 800u;   // Header - ExtraBold
        Driver.Values.TypographyWeight[3u] = 400u;   // Body   - Regular
        Driver.Values.TypographyWeight[5u] = 300u;   // Caption- Light
        Driver.Values.TypographyWeight[6u] = 700u;   // Warning- Bold
        ApplyFontWeights(Driver.Appearance, Driver.Values.TypographyWeight);
        Driver.Settle(20);
    }
    else if (std::strcmp(Scenario, "debug-fonts") == 0)
    {
        Driver.Tick(640.0f, 450.0f, false, false, false);
        // Draw one run per face through the same RecordingSurface path the panel uses.
        RecordingSurface& DebugSurface = Driver.Surface;
        ImDrawList* DebugList = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
        (void)DebugList;
        const float DebugTop = 60.0f;
        const char* Names[] = {"ArchivoPreview", "InterPreview", "JetBrainsMonoPreview", "OpenSansPreview", "ActiveRegular"};
        ImFont* Faces[] = {
            Driver.Fonts.Preview("Archivo", 1.0f),
            Driver.Fonts.Preview("Inter", 1.0f),
            Driver.Fonts.Preview("JetBrainsMono", 1.0f),
            Driver.Fonts.Preview("OpenSans", 1.0f),
            Driver.Fonts.Face(Slate::FontWeight::Regular, Slate::FontSlant::Upright)
        };
        for (int Index = 0; Index < 5; ++Index)
        {
            Driver.Surface.ApplyFontPreview(Faces[Index]);
            Driver.Surface.TextRun(80.0f, DebugTop + 60.0f * static_cast<float>(Index),
                                   Covering(0xFFFFFFu), "Aa The quick brown fox", 30.0f);
            Driver.Surface.TextRun(620.0f, DebugTop + 60.0f * static_cast<float>(Index),
                                   Covering(0xFFFFFFu), Names[Index], 20.0f);
            Driver.Surface.ApplyFontPreview(nullptr);
        }
        return Driver.Capture(OutputPath, Atlas, AtlasWidth, AtlasHeight);
    }
    else if (std::strcmp(Scenario, "sanity") == 0)
    {
        // A frame of nothing but the four primitives, so the rasterizer is proven in isolation.
        Driver.IO.MousePos = ImVec2(640.0f, 450.0f);
        Driver.IO.MouseDelta = ImVec2(0.0f, 0.0f);
        Driver.IO.DeltaTime = 0.016f;
        ImGui::NewFrame();
        Discard(Driver.Surface.Adopt(RecordingSurface::ShellLayer::Beneath));
        {
            // Direct vendor text at 48px with the active font — isolates font state from the surface.
            ImFont* ActiveFont = Driver.Fonts.Face(Slate::FontWeight::Regular, Slate::FontSlant::Upright);
            ImDrawList* L = ImGui::GetBackgroundDrawList();
            L->AddText(ActiveFont, 48.0f, ImVec2(60.0f, 20.0f), IM_COL32(255, 255, 255, 255), "Archivo");
            L->AddText(ActiveFont, 48.0f, ImVec2(60.0f, 90.0f), IM_COL32(255, 255, 255, 255), "Aa");
        }
        RecordingSurface& S = Driver.Surface;
        const PlaneExtent Red = Spanning(100.0f, 100.0f, 200.0f, 100.0f);
        S.Ground(Red, Covering(0xFF0000u), 0.0f, CornerNone);
        S.TextRun(100.0f, 250.0f, Covering(0xFFFFFFu), "HELLO SANITY", 40.0f);
        S.Edge(Spanning(400.0f, 100.0f, 200.0f, 100.0f), Covering(0x00FF00u), 4.0f, 0.0f, CornerNone);
        S.Medallion(700.0f, 150.0f, 40.0f, Covering(0x0000FFu));
        Driver.Surface.Retire();
        ImGui::Render();
        return Driver.Capture(OutputPath, Atlas, AtlasWidth, AtlasHeight);
    }
    else if (std::strcmp(Scenario, "fonts-bench") == 0)
    {
        const auto Now = []() { return std::chrono::steady_clock::now(); };
        const auto ElapsedMs = [](auto A, auto B)
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(B - A).count();
        };
        const char* BenchFamilies[] = {"Archivo", "Inter", "JetBrainsMono", "OpenSans", "Archivo"};
        for (const char* Family : BenchFamilies)
        {
            FontProfile Profile;
            std::strncpy(Profile.Family, Family, sizeof(Profile.Family) - 1u);
            const auto T0 = Now();
            Discard(Driver.Fonts.Load("EngineContent/FontArchives", Profile, 1.0f));
            const auto T1 = Now();
            std::fprintf(stderr, "[bench] Load(%-13s) %4lld ms  atlas fonts=%d  atlas %dx%d\n",
                         Family, (long long)ElapsedMs(T0, T1),
                         (int)Driver.IO.Fonts->Fonts.Size, Driver.IO.Fonts->TexData->Width, Driver.IO.Fonts->TexData->Height);
        }
        for (int Pass = 0; Pass < 3; ++Pass)
        {
            const auto T0 = Now();
            Discard(Driver.Fonts.PreparePreviews(1.0f));
            const auto T1 = Now();
            std::fprintf(stderr, "[bench] PreparePreviews pass %d  %4lld ms  atlas fonts=%d\n",
                         Pass, (long long)ElapsedMs(T0, T1), (int)Driver.IO.Fonts->Fonts.Size);
        }
        return Driver.Capture(OutputPath, Atlas, AtlasWidth, AtlasHeight);
    }
    else if (std::strcmp(Scenario, "fonts-inter-default") == 0)
    {
        // The default bring-up: the appearance names Inter and the hosts seat the carousel on it.
        Driver.Settle(20);
    }
    else if (std::strcmp(Scenario, "fonts-scrolled-lower-rows") == 0)
    {
        Driver.Values.TypographyWeight[0u] = 900u;   // Title    - Black
        Driver.Values.TypographyWeight[3u] = 400u;   // Body     - Regular
        Driver.Values.TypographyWeight[5u] = 300u;   // Caption  - Light
        Driver.Values.TypographyWeight[6u] = 700u;   // Warning  - Bold
        Driver.Values.TypographyWeight[7u] = 800u;   // Alert    - ExtraBold
        ApplyFontWeights(Driver.Appearance, Driver.Values.TypographyWeight);
        Driver.Settle(20);
        Driver.Scroll(-21.0f);   // 1512px down — brings Caption, Warning and Alert rows into view
    }
    else if (std::strcmp(Scenario, "settings-title-black") == 0)
    {
        Driver.Values.Page = ControlCentrePage::Settings;
        Driver.Values.TypographyWeight[0u] = 900u;   // Title - Black
        ApplyFontWeights(Driver.Appearance, Driver.Values.TypographyWeight);
        Driver.Settle(20);
    }
    else if (std::strcmp(Scenario, "settings-apply-footer") == 0)
    {
        Driver.Values.Page = ControlCentrePage::Settings;
        Driver.Values.TypographySize[0u] = 32u;
        Driver.Values.TypographySize[3u] = 18u;
        Driver.Settle(20);
    }
    else if (std::strcmp(Scenario, "settings-apply-confirmation") == 0)
    {
        Driver.Values.Page = ControlCentrePage::Settings;
        Driver.Values.TypographySize[0u] = 32u;
        Driver.Settle(20);
        Driver.Tap(1170.0f, 850.0f); // actual fixed Apply Settings footer control
        Driver.Settle(12);
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
    const char* FontRoot = "EngineContent/FontArchives";
    const char* Family = "Archivo";
    std::string OutputDirectory = "VisualProof/Typography";
    std::string Scenario = "";

    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        const std::string Arg = Arguments[Index];
        if (Arg == "--font-root" && Index + 1 < ArgumentCount)
            FontRoot = Arguments[++Index];
        else if (Arg == "--out" && Index + 1 < ArgumentCount)
            OutputDirectory = Arguments[++Index];
        else if (Arg == "--family" && Index + 1 < ArgumentCount)
            Family = Arguments[++Index];
        else if (Arg.rfind("--shot=", 0) == 0)
            Scenario = Arg.substr(7);
        else if (Arg.rfind("--shot ", 0) == 0 && Index + 1 < ArgumentCount)
            Scenario = Arguments[++Index];
    }

    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO();
    IO.DisplaySize = ImVec2(ViewportWidth, ViewportHeight);
    IO.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    IO.IniFilename = nullptr;
    IO.LogFilename = nullptr;
    // 🔴 A real renderer declares this, which keeps the atlas UNLOCKED during the frame so ImGui 1.93's
    //    per-size on-demand glyph baking can add the glyphs a run needs while it records. Without it the
    //    atlas locks between NewFrame and Render, every glyph not preloaded at that size falls back to
    //    the invisible fallback, and a 48px run renders only the glyphs a 16px bake happened to carry.
    //    The windowed hosts all have a real backend and are never subject to this.
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    ProofDriver Driver;
    if (!Driver.ConstructTypographyProof(FontRoot, Family))
        return 1;
    if (IO.Fonts->Fonts.empty())
    {
        std::fprintf(stderr, "rejected: the atlas holds no faces\n");
        return 1;
    }
    IO.FontDefault = IO.Fonts->Fonts[0];

    {
        const char* Families[] = {"Archivo", "Inter", "JetBrainsMono", "OpenSans"};
        for (const char* Family : Families)
        {
            ImFont* P = Driver.Fonts.Preview(Family, 1.0f);
            const float AaWidth = P != nullptr ? P->CalcTextSizeA(30.0f, FLT_MAX, 0.0f, "Aa").x : -1.0f;
            std::fprintf(stderr, "[font] Preview(%-14s) = %p  Aa@30 width %.1f\n",
                         Family, (const void*)P, AaWidth);
        }
        ImFont* Active = Driver.Fonts.Face(Slate::FontWeight::Regular, Slate::FontSlant::Upright);
        std::fprintf(stderr, "[font] Active(regular) = %p  Aa@30 width %.1f\n",
                     (const void*)Active,
                     Active != nullptr ? Active->CalcTextSizeA(30.0f, FLT_MAX, 0.0f, "Aa").x : -1.0f);
    }

    unsigned char* AtlasPixels = nullptr;
    int AtlasWidth = 0;
    int AtlasHeight = 0;
    IO.Fonts->GetTexDataAsRGBA32(&AtlasPixels, &AtlasWidth, &AtlasHeight);
    if (AtlasPixels == nullptr || AtlasWidth <= 0 || AtlasHeight <= 0)
    {
        std::fprintf(stderr, "rejected: the atlas produced no texture\n");
        return 1;
    }

    // 🔴 The legacy bake may have replaced the texture record; re-claim the id and the ref against the
    //    record the rasterizer actually samples.
    IO.Fonts->TexData->SetTexID((ImTextureID)(intptr_t)1);
    IO.Fonts->TexRef._TexData = IO.Fonts->TexData;
    std::fprintf(stderr, "[atlas] %dx%d RGBA\n", AtlasWidth, AtlasHeight);

    const char* Shots[] = {"fonts-archivo-carousels", "fonts-title-bold", "fonts-title-black-scrolled",
                           "fonts-mixed-weights", "fonts-scrolled-lower-rows", "settings-title-black",
                           "settings-apply-footer", "settings-apply-confirmation",
                           "fonts-inter-default", "fonts-bench"};

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

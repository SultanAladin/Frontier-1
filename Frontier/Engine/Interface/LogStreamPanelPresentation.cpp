/*==========================================================================================================================================
                                                NATIVE PRESENTATION OF THE LOG STREAM PANEL
                                     ------------------------------------------------------------------
                          Exact 1:1 visual port of the approved HTML prototype (LogStreamPanel.html).
                      The dropdown menus and the dock/panel frame remain the upstream reusable components:
                        DropdownControlSurface draws every menu; PanelDockspace owns the window frame.
==========================================================================================================================================*/

// Native presentation of the Log Stream panel: the approved prototype, pixel for pixel, on the shared kit.

#include "LogStreamPanel.h"

// This translation unit compiles only with the interface SDK present (Engine/Interface + vendored ImGui from
//    the upstream checkout): define FRONTIER_WITH_INTERFACE_SDK and supply those include roots and libraries.
//    Without the SDK it compiles to the empty presentation below so portable builds keep linking.
#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Diagnostics/LogStream.h"
#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/DropdownControls.h"
#include "Engine/Interface/TypefaceRegistry.h"
#include "LogStreamPanelRegistration.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

ImVec4 ToImVec4(LogColourSpectrum Spectrum) noexcept
{
    return ImVec4(Spectrum.Red, Spectrum.Green, Spectrum.Blue, Spectrum.Alpha);
}

const char* CeilingText(LogCeiling Ceiling) noexcept
{
    switch (Ceiling)
    {
        case LogCeiling::FatalOnly: return "Fatal only";
        case LogCeiling::Error:     return "Error";
        case LogCeiling::Warning:   return "Warning";
        case LogCeiling::Display:   return "Display";
        case LogCeiling::Log:       return "Log";
        case LogCeiling::Verbose:   return "Verbose";
    }
    return "Log";
}

const char* VerbosityDetailText(LogVerbosity Verbosity) noexcept
{
    switch (Verbosity)
    {
        case LogVerbosity::Display: return "display";
        case LogVerbosity::Verbose: return "verbose";
        case LogVerbosity::Log:     break;
    }
    return "log";
}

const char* SeverityDetailText(LogSeverity Severity) noexcept
{
    switch (Severity)
    {
        case LogSeverity::Warning: return "warning";
        case LogSeverity::Error:   return "error";
        case LogSeverity::Fatal:   return "fatal";
        case LogSeverity::Info:    break;
    }
    return "info";
}

std::string PlainEntry(const LogStreamVisibleEntry& Row, const LogCategoryRegistry& Categories, bool SessionClock, uint64_t SessionStart)
{
    const LogClockText   Clock    = FormatLogClock(Row.Entry.WallMicros, SessionStart, SessionClock);
    const LogCategory*   Category = Categories.Query(Row.Entry.Category);
    std::string          Text     = "[";
    Text += Clock.Main;
    Text += Clock.Millis;
    Text += "] [";
    Text += Category != nullptr ? Category->Name : "Unknown";
    Text += "] [";
    Text += LogSeverityText(Row.Entry.Severity);
    Text += "] ";
    Text += Row.Entry.Message;
    Text += " (";
    Text += Row.Entry.Source;
    Text += ")";
    return Text;
}

// ---------------------------------------------------------------------------
//   Prototype design tokens (LogStreamPanel.html :root), as draw colours.
// ---------------------------------------------------------------------------

constexpr ImU32 InkRgba(int Red, int Green, int Blue, int Alpha) noexcept
{
    return static_cast<ImU32>(Alpha) << 24 | static_cast<ImU32>(Blue) << 16 | static_cast<ImU32>(Green) << 8 |
           static_cast<ImU32>(Red);
}

constexpr ImU32 InkHex(int Rgb, int Alpha = 255) noexcept
{
    return InkRgba((Rgb >> 16) & 255, (Rgb >> 8) & 255, Rgb & 255, Alpha);
}

constexpr ImU32 InkWhite(float Alpha) noexcept
{
    return InkRgba(255, 255, 255, static_cast<int>(Alpha * 255.0f + 0.5f));
}

constexpr ImU32 InkFade(ImU32 Colour, float Scale) noexcept
{
    const int Alpha = static_cast<int>(static_cast<float>(static_cast<int>(Colour >> 24)) * Scale + 0.5f);
    return (Colour & 0x00ffffffu) | static_cast<ImU32>(Alpha) << 24;
}

constexpr ImU32 InkWithAlpha(ImU32 Colour, float Alpha) noexcept
{
    return (Colour & 0x00ffffffu) | static_cast<ImU32>(static_cast<int>(Alpha * 255.0f + 0.5f)) << 24;
}

struct LogPanelTheme
{
    static constexpr ImU32 PanelBg   = InkHex(0x0b100e);                    // rgba(11,16,14,.86) over the host page
    static constexpr ImU32 PanelEdge = InkWhite(0.08f);
    static constexpr ImU32 Surface   = InkHex(0x0e1411);
    static constexpr ImU32 Surface2  = InkHex(0x151d18);
    static constexpr ImU32 Surface3  = InkHex(0x1c2620);
    static constexpr ImU32 Line      = InkWhite(0.07f);
    static constexpr ImU32 LineSoft  = InkWhite(0.045f);
    static constexpr ImU32 RowSep    = InkWhite(0.028f);
    static constexpr ImU32 RowHover  = InkWhite(0.032f);
    static constexpr ImU32 Text      = InkHex(0xeef3f0);
    static constexpr ImU32 Muted     = InkHex(0x97a29b);
    static constexpr ImU32 Faint     = InkHex(0x5f6b64);
    static constexpr ImU32 Mint      = InkHex(0x2fe6a7);
    static constexpr ImU32 MintDeep  = InkHex(0x053322);
    static constexpr ImU32 MintDim   = InkRgba(47, 230, 167, 33);           // rgba(47,230,167,.13)
    static constexpr ImU32 Amber     = InkHex(0xffb454);
    static constexpr ImU32 AmberDim  = InkRgba(255, 180, 84, 31);
    static constexpr ImU32 Coral     = InkHex(0xff6d6d);
    static constexpr ImU32 CoralDim  = InkRgba(255, 109, 109, 31);
    static constexpr ImU32 Red       = InkHex(0xff4747);
    static constexpr ImU32 ChipOnBg  = InkHex(0xe9f2ed);
    static constexpr ImU32 ChipOnInk = InkHex(0x0b120e);
    static constexpr ImU32 MsgText   = InkHex(0xdbe4de);
    static constexpr ImU32 MsgWarn   = InkHex(0xf0e2cd);
    static constexpr ImU32 MsgErr    = InkHex(0xf3d9d9);
    static constexpr ImU32 DetailTx  = InkHex(0xcfd8d1);
    static constexpr ImU32 MsRow     = InkHex(0x45524a);
    static constexpr ImU32 MsHead    = InkHex(0x3f4a44);
    static constexpr ImU32 WarnInk   = InkHex(0x10130f);
};

// ---------------------------------------------------------------------------
//   DM Sans faces through the upstream typeface registry (host default font
//   when the family is not supplied in FontArchives).
// ---------------------------------------------------------------------------

struct LogPanelFaces
{
    ImFont* Light    = nullptr;
    ImFont* Regular  = nullptr;
    ImFont* Medium   = nullptr;
    ImFont* SemiBold = nullptr;
};

LogPanelFaces ResolveLogFaces() noexcept
{
    LogPanelFaces Faces;
    Faces.Light = Faces.Regular = Faces.Medium = Faces.SemiBold = ImGui::GetFont();
    TypefaceRegistry* Registry = TypefaceRegistry::QueryCurrent();
    if (Registry == nullptr)
    {
        return Faces;
    }
    const int32_t Family = Registry->ResolveFamilyIndex("DM Sans");
    if (Family < 0)
    {
        return Faces;
    }
    const auto Grab = [Registry, Family](FontWeightCategory Weight) -> ImFont* {
        const TypefaceFace* Face = Registry->Resolve(static_cast<uint32_t>(Family), Weight);
        return Face != nullptr ? static_cast<ImFont*>(Face->Handle) : nullptr;
    };
    if (ImFont* Face = Grab(FontWeightCategory::Light))
    {
        Faces.Light = Face;
    }
    if (ImFont* Face = Grab(FontWeightCategory::Regular))
    {
        Faces.Regular = Face;
    }
    if (ImFont* Face = Grab(FontWeightCategory::Medium))
    {
        Faces.Medium = Face;
    }
    if (ImFont* Face = Grab(FontWeightCategory::SemiBold))
    {
        Faces.SemiBold = Face;
    }
    return Faces;
}

// ---------------------------------------------------------------------------
//   Text measurement and drawing at exact prototype sizes.
// ---------------------------------------------------------------------------

float TextWidth(ImFont* Font, float Size, const char* Begin, const char* End = nullptr) noexcept
{
    return Font->CalcTextSizeA(Size, 3.402823466e+38f, 0.0f, Begin, End).x;
}

void DrawText(ImDrawList* Ink, ImFont* Font, float Size, ImVec2 Position, ImU32 Colour, const char* Begin,
              const char* End = nullptr)
{
    Ink->AddText(Font, Size, Position, Colour, Begin, End);
}

// Letter-tracked uppercase labels (CSS letter-spacing). ASCII only by design.
float TrackedWidth(ImFont* Font, float Size, const char* Text, float Tracking) noexcept
{
    float Width = 0.0f;
    for (const char* Cursor = Text; *Cursor != '\0'; ++Cursor)
    {
        Width += TextWidth(Font, Size, Cursor, Cursor + 1) + Tracking;
    }
    return Width > Tracking ? Width - Tracking : 0.0f;
}

float DrawTracked(ImDrawList* Ink, ImFont* Font, float Size, ImVec2 Position, ImU32 Colour, const char* Text,
                  float Tracking)
{
    float X = Position.x;
    for (const char* Cursor = Text; *Cursor != '\0'; ++Cursor)
    {
        Ink->AddText(Font, Size, ImVec2(X, Position.y), Colour, Cursor, Cursor + 1);
        X += TextWidth(Font, Size, Cursor, Cursor + 1) + Tracking;
    }
    return X - Position.x - (Text[0] != '\0' ? Tracking : 0.0f);
}

std::string FormatGrouped(uint64_t Value)
{
    char Digits[32] = {};
    std::snprintf(Digits, sizeof(Digits), "%llu", static_cast<unsigned long long>(Value));
    const std::string Raw = Digits;
    std::string       Out;
    for (size_t Index = 0; Index < Raw.size(); ++Index)
    {
        if (Index > 0 && (Raw.size() - Index) % 3 == 0)
        {
            Out += ',';
        }
        Out += Raw[Index];
    }
    return Out;
}

uint64_t NowWallMicros() noexcept
{
    const auto Now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(Now).count());
}

// ---------------------------------------------------------------------------
//   Word wrap + ellipsis + search-match spans (the prototype's <mark> runs).
// ---------------------------------------------------------------------------

struct TextSpan
{
    const char* Begin;
    const char* End;
};

std::vector<TextSpan> WrapTextLines(ImFont* Font, float Size, const char* Text, float Width)
{
    std::vector<TextSpan> Lines;
    const char*           End = Text + std::strlen(Text);
    const char*           Cursor = Text;
    while (Cursor < End)
    {
        const char* Stop = Font->CalcWordWrapPosition(Size, Cursor, End, Width);
        if (Stop <= Cursor)
        {
            Stop = Cursor + 1;
        }
        Lines.push_back(TextSpan{Cursor, Stop});
        Cursor = Stop;
        while (Cursor < End && (*Cursor == ' ' || *Cursor == '\n'))
        {
            if (*Cursor == '\n')
            {
                ++Cursor;
                break;
            }
            ++Cursor;
        }
    }
    if (Lines.empty())
    {
        Lines.push_back(TextSpan{Text, Text});
    }
    return Lines;
}

std::string EllipsizeText(ImFont* Font, float Size, const char* Text, float Width)
{
    if (TextWidth(Font, Size, Text) <= Width)
    {
        return Text;
    }
    const std::string Full    = Text;
    const std::string Ellipsis = "\xE2\x80\xA6";
    size_t            Count    = Full.size();
    while (Count > 0)
    {
        size_t Cut = Count;
        while (Cut > 0 && (static_cast<unsigned char>(Full[Cut]) & 0xc0) == 0x80)
        {
            --Cut;
        }
        if (TextWidth(Font, Size, (Full.substr(0, Cut) + Ellipsis).c_str()) <= Width)
        {
            return Full.substr(0, Cut) + Ellipsis;
        }
        Count = Cut > 0 ? Cut - 1 : 0;
    }
    return Ellipsis;
}

std::vector<TextSpan> FindMatches(const char* Text, const std::string& QueryLower)
{
    std::vector<TextSpan> Spans;
    if (QueryLower.empty())
    {
        return Spans;
    }
    std::string Lower = Text;
    std::transform(Lower.begin(), Lower.end(), Lower.begin(), [](unsigned char Glyph) {
        return static_cast<char>(std::tolower(Glyph));
    });
    size_t From = 0;
    for (;;)
    {
        const size_t At = Lower.find(QueryLower, From);
        if (At == std::string::npos)
        {
            break;
        }
        Spans.push_back(TextSpan{Text + At, Text + At + QueryLower.size()});
        From = At + QueryLower.size();
    }
    return Spans;
}

// One text line with <mark> rectangles behind matched spans (span colours from the prototype CSS).
void DrawMessageLine(ImDrawList* Ink, ImFont* Font, float Size, ImVec2 Position, ImU32 Colour, const char* Begin,
                     const char* End, const std::vector<TextSpan>& Matches)
{
    for (const TextSpan& Match : Matches)
    {
        if (Match.Begin < Begin || Match.Begin >= End)
        {
            continue;
        }
        const char* MatchEnd = Match.End < End ? Match.End : End;
        const float StartX   = Position.x + TextWidth(Font, Size, Begin, Match.Begin);
        const float SpanW    = TextWidth(Font, Size, Match.Begin, MatchEnd);
        Ink->AddRectFilled(ImVec2(StartX - 1.0f, Position.y - 0.5f),
                           ImVec2(StartX + SpanW + 1.0f, Position.y + Size + 1.5f),
                           InkRgba(47, 230, 167, 64), 3.0f);
    }
    Ink->AddText(Font, Size, Position, Colour, Begin, End);
    for (const TextSpan& Match : Matches)
    {
        if (Match.Begin < Begin || Match.Begin >= End)
        {
            continue;
        }
        const char* MatchEnd = Match.End < End ? Match.End : End;
        const float StartX   = Position.x + TextWidth(Font, Size, Begin, Match.Begin);
        Ink->AddText(Font, Size, ImVec2(StartX, Position.y), LogPanelTheme::Text, Match.Begin, MatchEnd);
    }
}

// ---------------------------------------------------------------------------
//   Prototype icon set, stroked/filled in the 24-unit SVG box.
// ---------------------------------------------------------------------------

struct IconBox
{
    ImVec2 Min;
    float  Scale;
    ImVec2 At(float X, float Y) const noexcept { return ImVec2(Min.x + X * Scale, Min.y + Y * Scale); }
};

IconBox MakeIconBox(ImVec2 Centre, float Size) noexcept
{
    return IconBox{ImVec2(Centre.x - Size * 0.5f, Centre.y - Size * 0.5f), Size / 24.0f};
}

void StrokePath(ImDrawList* Ink, const IconBox& Box, const ImVec2* Points, int Count, ImU32 Colour, float Width)
{
    ImVec2 Mapped[8];
    for (int Index = 0; Index < Count && Index < 8; ++Index)
    {
        Mapped[Index] = Box.At(Points[Index].x, Points[Index].y);
    }
    Ink->AddPolyline(Mapped, Count < 8 ? Count : 8, Colour, 0, Width * Box.Scale);
}

void DrawIconPause(ImDrawList* Ink, ImVec2 Centre, float Size, ImU32 Colour)
{
    const IconBox Box = MakeIconBox(Centre, Size);
    Ink->AddRectFilled(Box.At(6, 5), Box.At(10, 19), Colour, 1.2f * Box.Scale);
    Ink->AddRectFilled(Box.At(14, 5), Box.At(18, 19), Colour, 1.2f * Box.Scale);
}

void DrawIconPlay(ImDrawList* Ink, ImVec2 Centre, float Size, ImU32 Colour)
{
    const IconBox Box = MakeIconBox(Centre, Size);
    Ink->AddTriangleFilled(Box.At(8, 5.2f), Box.At(8, 18.8f), Box.At(20, 12), Colour);
}

void DrawIconTrash(ImDrawList* Ink, ImVec2 Centre, float Size, ImU32 Colour)
{
    const IconBox Box = MakeIconBox(Centre, Size);
    const ImVec2  Body[]  = {{6.5f, 7}, {7.3f, 18.2f}, {9.3f, 20}, {14.7f, 20}, {16.7f, 18.2f}, {17.5f, 7}};
    const ImVec2  Handle[] = {{9.5f, 7}, {9.5f, 5}, {11, 3.5f}, {13, 3.5f}, {14.5f, 5}, {14.5f, 7}};
    const ImVec2  Lid[]    = {{4, 7}, {20, 7}};
    const ImVec2  SlotA[]  = {{10, 11}, {10, 16}};
    const ImVec2  SlotB[]  = {{14, 11}, {14, 16}};
    StrokePath(Ink, Box, Body, 6, Colour, 1.8f);
    StrokePath(Ink, Box, Handle, 6, Colour, 1.8f);
    StrokePath(Ink, Box, Lid, 2, Colour, 1.8f);
    StrokePath(Ink, Box, SlotA, 2, Colour, 1.8f);
    StrokePath(Ink, Box, SlotB, 2, Colour, 1.8f);
}

void DrawIconExport(ImDrawList* Ink, ImVec2 Centre, float Size, ImU32 Colour)
{
    const IconBox Box = MakeIconBox(Centre, Size);
    const ImVec2  Drop[]  = {{12, 4}, {12, 14.5f}};
    const ImVec2  Head[]  = {{7.8f, 10.8f}, {12, 15}, {16.2f, 10.8f}};
    const ImVec2  Tray[]  = {{5, 19.5f}, {19, 19.5f}};
    StrokePath(Ink, Box, Drop, 2, Colour, 1.8f);
    StrokePath(Ink, Box, Head, 3, Colour, 1.8f);
    StrokePath(Ink, Box, Tray, 2, Colour, 1.8f);
}

void DrawIconOverflow(ImDrawList* Ink, ImVec2 Centre, float Size, ImU32 Colour)
{
    const IconBox Box = MakeIconBox(Centre, Size);
    Ink->AddCircleFilled(Box.At(5.5f, 12), 1.7f * Box.Scale, Colour);
    Ink->AddCircleFilled(Box.At(12, 12), 1.7f * Box.Scale, Colour);
    Ink->AddCircleFilled(Box.At(18.5f, 12), 1.7f * Box.Scale, Colour);
}

void DrawIconFunnel(ImDrawList* Ink, ImVec2 Centre, float Size, ImU32 Colour)
{
    const IconBox Box = MakeIconBox(Centre, Size);
    const ImVec2  A[] = {{4, 6.5f}, {20, 6.5f}};
    const ImVec2  B[] = {{7, 12}, {17, 12}};
    const ImVec2  C[] = {{10, 17.5f}, {14, 17.5f}};
    StrokePath(Ink, Box, A, 2, Colour, 1.8f);
    StrokePath(Ink, Box, B, 2, Colour, 1.8f);
    StrokePath(Ink, Box, C, 2, Colour, 1.8f);
}

void DrawIconChevron(ImDrawList* Ink, ImVec2 Centre, float Size, ImU32 Colour, bool Open)
{
    const IconBox Box = MakeIconBox(Centre, Size);
    static const ImVec2 Downward[3] = {{5.5f, 9.5f}, {12, 16}, {18.5f, 9.5f}};
    static const ImVec2 Upward[3]   = {{5.5f, 14.5f}, {12, 8}, {18.5f, 14.5f}};
    StrokePath(Ink, Box, Open ? Upward : Downward, 3, Colour, 2.2f);
}

void DrawIconSearch(ImDrawList* Ink, ImVec2 Centre, float Size, ImU32 Colour)
{
    const IconBox Box = MakeIconBox(Centre, Size);
    Ink->AddCircle(Box.At(11, 11), 6.5f * Box.Scale, Colour, 24, 2.0f * Box.Scale);
    const ImVec2 Handle[] = {{16, 16}, {20.5f, 20.5f}};
    StrokePath(Ink, Box, Handle, 2, Colour, 2.0f);
}

void DrawIconCross(ImDrawList* Ink, ImVec2 Centre, float Size, ImU32 Colour)
{
    const IconBox Box = MakeIconBox(Centre, Size);
    const ImVec2  A[] = {{6, 6}, {18, 18}};
    const ImVec2  B[] = {{18, 6}, {6, 18}};
    StrokePath(Ink, Box, A, 2, Colour, 2.6f);
    StrokePath(Ink, Box, B, 2, Colour, 2.6f);
}

void DrawIconArrowDown(ImDrawList* Ink, ImVec2 Centre, float Size, ImU32 Colour)
{
    const IconBox Box = MakeIconBox(Centre, Size);
    const ImVec2  Stem[] = {{12, 4.5f}, {12, 17.5f}};
    const ImVec2  Head[] = {{6.5f, 12.5f}, {12, 18}, {17.5f, 12.5f}};
    StrokePath(Ink, Box, Stem, 2, Colour, 2.6f);
    StrokePath(Ink, Box, Head, 3, Colour, 2.6f);
}

void DrawWarnGlyph(ImDrawList* Ink, ImVec2 Centre, float Size, ImU32 Fill)
{
    const IconBox Box = MakeIconBox(Centre, Size);
    Ink->AddTriangleFilled(Box.At(12, 3.6f), Box.At(22, 20), Box.At(2, 20), Fill);
    Ink->AddRectFilled(Box.At(11.1f, 9.5f), Box.At(12.9f, 14.9f), LogPanelTheme::WarnInk, 0.9f * Box.Scale);
    Ink->AddCircleFilled(Box.At(12, 17.2f), 1.15f * Box.Scale, LogPanelTheme::WarnInk);
}

void DrawBrandBars(ImDrawList* Ink, ImVec2 Centre)
{
    const IconBox Box = MakeIconBox(Centre, 16.0f);
    const ImU32   Colour = InkWithAlpha(LogPanelTheme::Mint, 0.95f);
    const float   Xs[4]     = {5.0f, 10.2f, 15.4f, 20.6f};
    const float   Bottom[4] = {18.5f, 18.5f, 13.5f, 10.5f};
    for (int Index = 0; Index < 4; ++Index)
    {
        Ink->AddLine(Box.At(Xs[Index], 5.5f), Box.At(Xs[Index], Bottom[Index]), Colour, 2.4f * Box.Scale);
    }
}

// ---------------------------------------------------------------------------
//   Pill surface + invisible hit target (the prototype's pill controls).
// ---------------------------------------------------------------------------

bool PillTarget(const char* Id, ImVec2 Minimum, ImVec2 Size, bool& Hovered, const char* Tooltip = nullptr)
{
    ImGui::SetCursorScreenPos(Minimum);
    const bool Clicked = ImGui::InvisibleButton(Id, Size);
    Hovered = ImGui::IsItemHovered();
    if (Hovered && Tooltip != nullptr)
    {
        ImGui::SetTooltip("%s", Tooltip);
    }
    return Clicked;
}

void PillSurface(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, ImU32 Background, ImU32 Border, float Radius)
{
    if ((Background >> 24) != 0)
    {
        Ink->AddRectFilled(Minimum, Maximum, Background, Radius);
    }
    if ((Border >> 24) != 0)
    {
        Ink->AddRect(Minimum, Maximum, Border, Radius);
    }
}

// Menu section caption inside the reusable dropdown surface (prototype .menu-label).
void MenuCaption(const LogPanelFaces& Faces, const char* Text)
{
    ImDrawList*  Ink = ImGui::GetWindowDrawList();
    const ImVec2 At = ImGui::GetCursorScreenPos();
    DrawTracked(Ink, Faces.Medium, 10.5f, ImVec2(At.x + 10.0f, At.y + 8.0f), LogPanelTheme::Faint, Text,
                10.5f * 0.13f);
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 27.0f));
}

void MenuSeparator()
{
    ImDrawList*  Ink    = ImGui::GetWindowDrawList();
    const ImVec2 At     = ImGui::GetCursorScreenPos();
    const float  Width  = ImGui::GetContentRegionAvail().x;
    Ink->AddRectFilled(ImVec2(At.x + 4.0f, At.y + 6.0f), ImVec2(At.x + Width - 4.0f, At.y + 7.0f),
                       LogPanelTheme::LineSoft);
    ImGui::Dummy(ImVec2(Width, 13.0f));
}

void MenuTrailingText(const LogPanelFaces& Faces, const char* Text, float Size)
{
    ImDrawList*  Ink     = ImGui::GetWindowDrawList();
    const ImVec2 Maximum = ImGui::GetItemRectMax();
    const ImVec2 Minimum = ImGui::GetItemRectMin();
    const float  Width   = TextWidth(Faces.Regular, Size, Text);
    DrawText(Ink, Faces.Regular, Size,
             ImVec2(Maximum.x - 12.0f - Width, Minimum.y + (Maximum.y - Minimum.y - Size) * 0.5f),
             LogPanelTheme::Faint, Text);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PANEL REGISTRATION
//------------------------------------------------------------------------------------------------------------------------

DockPanelSpecification MakeLogStreamPanelSpecification(LogStreamPanel& Panel)
{
    DockPanelSpecification Specification;
    Specification.Key              = "LogStream";
    Specification.Caption          = "Log Stream";
    Specification.ConstructContent = [&Panel]() { Panel.ConstructInterface(); };
    Specification.Open             = true;
    return Specification;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERFACE RECORDING
//------------------------------------------------------------------------------------------------------------------------

void LogStreamPanel::ConstructInterface()
{
    LogCategoryRegistry& Categories = LogCategories();
    const LogPanelFaces  Faces      = ResolveLogFaces();
    using Theme                     = LogPanelTheme;

    // Presentation-local edit buffers. One Log Stream panel is hosted per workspace, so function-local
    //    statics are sufficient; a second hosted instance would need these promoted into the panel.
    static std::array<char, 256> SearchBuffer{};
    static std::string           SearchSynchronised;
    static std::array<char, 64>  CategoryNameBuffer{};
    static LogColour             PendingColour   = LogColour::Purple;
    static bool                  ScrollPrimed    = false;
    static bool                  SearchFocused   = false;

    const ImVec2 WindowPosition = ImGui::GetWindowPos();
    const ImVec2 WindowSize     = ImGui::GetWindowSize();
    const float  PL             = WindowPosition.x;
    const float  PT             = WindowPosition.y;
    const float  PR             = PL + WindowSize.x;
    const float  PB             = PT + WindowSize.y;
    const float  CL             = PL + 22.0f;                        // [px] - Prototype content inset
    const float  CR             = PR - 22.0f;
    const bool   Narrow         = WindowSize.x < 900.0f;             // [@media max-width:900px]
    ImDrawList*  Ink            = ImGui::GetWindowDrawList();

    Ink->AddRectFilled(WindowPosition, ImVec2(PR, PB), Theme::PanelBg);
    Ink->AddRect(WindowPosition, ImVec2(PR, PB), Theme::PanelEdge);

    // -- Header ------------------------------------------------------------------------------------------------------
    {
        const float Top = PT + 18.0f;
        const float CY  = Top + 18.0f;                               // [px] - 36px row centre

        Ink->AddRectFilled(ImVec2(CL, CY - 15.0f), ImVec2(CL + 30.0f, CY + 15.0f), Theme::Surface2, 9.0f);
        Ink->AddRect(ImVec2(CL, CY - 15.0f), ImVec2(CL + 30.0f, CY + 15.0f), Theme::Line, 9.0f);
        DrawBrandBars(Ink, ImVec2(CL + 15.0f, CY));

        const float TitleX = CL + 30.0f + 14.0f;
        DrawText(Ink, Faces.Medium, 21.0f, ImVec2(TitleX, CY - 10.5f), Theme::Text, "Log Stream");
        const float TitleW = TextWidth(Faces.Medium, 21.0f, "Log Stream");

        // Live pill (the prototype's trailing session label is demo content, not engine state).
        const float   LiveX  = TitleX + TitleW + 14.0f;
        const char*   Word   = PausedFlag ? "Paused" : "Live";
        const float   WordW  = TextWidth(Faces.Medium, 12.5f, Word);
        const float   LiveW  = 13.0f + 7.0f + 8.0f + WordW + 13.0f;
        const float   LiveH  = 27.0f;
        PillSurface(Ink, ImVec2(LiveX, CY - LiveH * 0.5f), ImVec2(LiveX + LiveW, CY + LiveH * 0.5f),
                    Theme::Surface2, Theme::Line, 999.0f);
        const ImVec2 DotCentre(LiveX + 13.0f + 3.5f, CY);
        if (!PausedFlag)
        {
            const double Phase = std::fmod(ImGui::GetTime(), 2.2) / 2.2;
            if (Phase < 0.7)
            {
                const float Spread = static_cast<float>(Phase / 0.7);
                Ink->AddCircleFilled(DotCentre, 3.5f + 7.0f * Spread,
                                     InkWithAlpha(Theme::Mint, 0.45f * (1.0f - Spread)));
            }
        }
        Ink->AddCircleFilled(DotCentre, 3.5f, PausedFlag ? Theme::Faint : Theme::Mint);
        DrawText(Ink, Faces.Medium, 12.5f, ImVec2(LiveX + 28.0f, CY - 6.25f), Theme::Text, Word);

        // Head clock.
        const LogClockText Clock = FormatLogClock(NowWallMicros(), 0, false);
        const float        ClockX = LiveX + LiveW + 14.0f;
        DrawText(Ink, Faces.Regular, 13.0f, ImVec2(ClockX, CY - 6.5f), Theme::Faint, Clock.Main);
        DrawText(Ink, Faces.Regular, 13.0f,
                 ImVec2(ClockX + TextWidth(Faces.Regular, 13.0f, Clock.Main), CY - 6.5f), Theme::MsHead, Clock.Millis);

        // Icon buttons: Pause / Clear / Export / Overflow. (No Popout: detaching is the dock tab's own gesture.)
        const bool OverflowOpen = ImGui::IsPopupOpen("##LogOverflowPopup");
        struct HeadAction
        {
            const char* Id;
            const char* Tooltip;
            int         Glyph;    // 0 pause/play, 1 trash, 2 export, 3 overflow
            bool        Active;
        };
        const HeadAction Actions[4] = {
            {"##LogHeadPause", "Pause stream (Space)", 0, PausedFlag},
            {"##LogHeadClear", "Clear stream", 1, false},
            {"##LogHeadExport", "Export visible entries (.log)", 2, false},
            {"##LogHeadOverflow", "More actions", 3, OverflowOpen},
        };
        float ButtonX = CR - (4 * 36.0f + 3 * 8.0f);
        for (const HeadAction& Action : Actions)
        {
            bool       Hovered = false;
            const bool Clicked = PillTarget(Action.Id, ImVec2(ButtonX, Top), ImVec2(36.0f, 36.0f), Hovered,
                                            Action.Tooltip);
            const ImVec2 Minimum(ButtonX, Top - (Hovered ? 1.0f : 0.0f));
            const ImVec2 Maximum(Minimum.x + 36.0f, Minimum.y + 36.0f);
            const ImU32  Background = Action.Active ? Theme::Mint : Theme::Surface2;
            const ImU32  Border     = Action.Active ? Theme::Mint : (Hovered ? InkWhite(0.16f) : Theme::Line);
            const ImU32  GlyphInk   = Action.Active ? Theme::MintDeep : (Hovered ? Theme::Text : Theme::Muted);
            PillSurface(Ink, Minimum, Maximum, Background, Border, 11.0f);
            const ImVec2 Centre((Minimum.x + Maximum.x) * 0.5f, (Minimum.y + Maximum.y) * 0.5f);
            switch (Action.Glyph)
            {
                case 0:
                    if (PausedFlag)
                    {
                        DrawIconPlay(Ink, Centre, 17.0f, GlyphInk);
                    }
                    else
                    {
                        DrawIconPause(Ink, Centre, 17.0f, GlyphInk);
                    }
                    break;
                case 1: DrawIconTrash(Ink, Centre, 17.0f, GlyphInk); break;
                case 2: DrawIconExport(Ink, Centre, 17.0f, GlyphInk); break;
                default: DrawIconOverflow(Ink, Centre, 17.0f, GlyphInk); break;
            }
            if (Clicked)
            {
                switch (Action.Glyph)
                {
                    case 0: ConfigurePaused(!PausedFlag); break;
                    case 1:
                        ClearView();
                        if (const LogCategory* Interface = Categories.QueryByName("Interface"))
                        {
                            WriteLog(Interface->Token, LogSeverity::Info, LogVerbosity::Display,
                                     "Stream cleared by user \xC2\xB7 previous entries retained in session file",
                                     "LogStreamPanelPresentation.cpp");
                        }
                        break;
                    case 2:
                    {
                        std::ofstream Out("FrontierLogStream.log", std::ios::trunc);
                        for (const LogStreamVisibleEntry& Row : Visible)
                        {
                            Out << PlainEntry(Row, Categories, SessionClock, SessionStartMicros) << '\n';
                        }
                        break;
                    }
                    default: ImGui::OpenPopup("##LogOverflowPopup"); break;
                }
            }
            ButtonX += 44.0f;
        }
    }
    float CursorY = PT + 68.0f;                                      // [px] - Header band: 18 + 36 + 14

    // -- Summary strip -----------------------------------------------------------------------------------------------
    {
        const float CardTop = CursorY + 2.0f;
        const float CardH   = 85.0f;                                 // [px] - 14 + (15 + 2 + 40) + 14
        const float LabelY  = CardTop + 14.0f;
        const float ValueY  = CardTop + 14.0f + 17.0f;
        Ink->AddRectFilled(ImVec2(CL, CardTop), ImVec2(CR, CardTop + CardH), Theme::Surface, 16.0f);
        Ink->AddRect(ImVec2(CL, CardTop), ImVec2(CR, CardTop + CardH), Theme::LineSoft, 16.0f);

        char RateBadge[32] = {};
        std::snprintf(RateBadge, sizeof(RateBadge), "+%u/s", QueryRateNow());
        char WarnBadge[32] = {};
        char ErrBadge[32]  = {};
        const uint64_t RecentWarningsCount = QueryWarningsLastMinute();
        const uint64_t RecentErrorsCount   = QueryErrorsLastMinute();
        if (RecentWarningsCount > 0)
        {
            std::snprintf(WarnBadge, sizeof(WarnBadge), "+%llu/min",
                          static_cast<unsigned long long>(RecentWarningsCount));
        }
        if (RecentErrorsCount > 0)
        {
            std::snprintf(ErrBadge, sizeof(ErrBadge), "+%llu/min",
                          static_cast<unsigned long long>(RecentErrorsCount));
        }

        struct MetricCell
        {
            const char* Label;
            uint64_t    Value;
            const char* Badge;
            ImU32       BadgeBg;
            ImU32       BadgeInk;
            ImU32       GlyphFill;                                    // 0 = no warning triangle
        };
        const MetricCell Cells[3] = {
            {"ENTRIES", TotalCount, RateBadge, Theme::MintDim, Theme::Mint, 0},
            {"WARNINGS", QuerySeverityCount(LogSeverity::Warning),
             RecentWarningsCount > 0 ? WarnBadge : "\xE2\x80\x94",
             RecentWarningsCount > 0 ? Theme::AmberDim : Theme::Surface3,
             RecentWarningsCount > 0 ? Theme::Amber : Theme::Faint, Theme::Amber},
            {"ERRORS", QuerySeverityCount(LogSeverity::Error) + QuerySeverityCount(LogSeverity::Fatal),
             RecentErrorsCount > 0 ? ErrBadge : "\xE2\x80\x94",
             RecentErrorsCount > 0 ? Theme::CoralDim : Theme::Surface3,
             RecentErrorsCount > 0 ? Theme::Coral : Theme::Faint, Theme::Coral},
        };

        float MetricX = CL + 18.0f;
        for (const MetricCell& Cell : Cells)
        {
            float LabelX = MetricX;
            if (Cell.GlyphFill != 0)
            {
                DrawWarnGlyph(Ink, ImVec2(MetricX + 6.5f, LabelY + 6.5f), 13.0f, Cell.GlyphFill);
                LabelX += 20.0f;
            }
            DrawTracked(Ink, Faces.Medium, 11.0f, ImVec2(LabelX, LabelY), Theme::Faint, Cell.Label, 11.0f * 0.13f);

            // Light 38px value with the trailing thousands group dimmed at 24px (prototype splitNum).
            const std::string Grouped = FormatGrouped(Cell.Value);
            std::string       Head    = Grouped;
            std::string       Fraction;
            if (Grouped.size() > 4)
            {
                Head     = Grouped.substr(0, Grouped.size() - 4);
                Fraction = Grouped.substr(Grouped.size() - 4);
            }
            const float Tracking = -38.0f * 0.03f;
            const float HeadW    = TrackedWidth(Faces.Light, 38.0f, Head.c_str(), Tracking);
            DrawTracked(Ink, Faces.Light, 38.0f, ImVec2(MetricX, ValueY), Theme::Text, Head.c_str(), Tracking);
            float ValueW = HeadW;
            if (!Fraction.empty())
            {
                DrawTracked(Ink, Faces.Light, 24.0f, ImVec2(MetricX + HeadW + Tracking, ValueY + 11.0f),
                            Theme::Faint, Fraction.c_str(), -24.0f * 0.03f);
                ValueW += Tracking + TrackedWidth(Faces.Light, 24.0f, Fraction.c_str(), -24.0f * 0.03f);
            }

            const float BadgeW = TextWidth(Faces.Medium, 11.5f, Cell.Badge) + 16.0f;
            const float BadgeX = MetricX + ValueW + 9.0f;
            const float BadgeY = ValueY + 38.0f * 0.8f - 16.0f;       // Baseline-aligned with the numeral
            Ink->AddRectFilled(ImVec2(BadgeX, BadgeY), ImVec2(BadgeX + BadgeW, BadgeY + 20.0f), Cell.BadgeBg, 7.0f);
            DrawText(Ink, Faces.Medium, 11.5f, ImVec2(BadgeX + 8.0f, BadgeY + 4.25f), Cell.BadgeInk, Cell.Badge);

            const float ContentW = std::max(118.0f, ValueW + 9.0f + BadgeW);
            const float DividerX = MetricX + ContentW + 26.0f;
            Ink->AddLine(ImVec2(DividerX, CardTop + 14.0f), ImVec2(DividerX, CardTop + CardH - 14.0f),
                         Theme::LineSoft);
            MetricX = DividerX + 26.0f;
        }

        // Sparkline: exact port of the prototype's canvas routine.
        const float SparkRight = CR - 18.0f;
        if (SparkRight - MetricX > 60.0f)
        {
            DrawTracked(Ink, Faces.Medium, 11.0f, ImVec2(MetricX, LabelY), Theme::Faint, "ENTRIES PER SECOND",
                        11.0f * 0.13f);
            char NowText[16] = {};
            char PeakText[16] = {};
            std::snprintf(NowText, sizeof(NowText), "%u", QueryRateNow());
            std::snprintf(PeakText, sizeof(PeakText), "%u", RatePeak);
            const float RateW = TextWidth(Faces.Medium, 13.0f, NowText) +
                                TextWidth(Faces.Regular, 13.0f, " now \xC2\xB7 ") +
                                TextWidth(Faces.Regular, 13.0f, PeakText) +
                                TextWidth(Faces.Regular, 13.0f, " peak");
            float RateX = SparkRight - RateW;
            const float RateY = LabelY - 2.0f;
            DrawText(Ink, Faces.Medium, 13.0f, ImVec2(RateX, RateY), Theme::Text, NowText);
            RateX += TextWidth(Faces.Medium, 13.0f, NowText);
            DrawText(Ink, Faces.Regular, 13.0f, ImVec2(RateX, RateY), Theme::Muted, " now \xC2\xB7 ");
            RateX += TextWidth(Faces.Regular, 13.0f, " now \xC2\xB7 ");
            DrawText(Ink, Faces.Regular, 13.0f, ImVec2(RateX, RateY), Theme::Muted, PeakText);
            RateX += TextWidth(Faces.Regular, 13.0f, PeakText);
            DrawText(Ink, Faces.Regular, 13.0f, ImVec2(RateX, RateY), Theme::Muted, " peak");

            const float X0 = MetricX;
            const float Y0 = CardTop + 14.0f + 15.0f + 4.0f;
            const float W  = SparkRight - X0;
            const float H  = CardH - 28.0f - 19.0f;
            const float GuideY = Y0 + H - (H - 6.0f) * 0.66f;
            for (float DashX = X0; DashX + 3.0f <= X0 + W; DashX += 7.0f)
            {
                Ink->AddLine(ImVec2(DashX, GuideY), ImVec2(DashX + 3.0f, GuideY), InkWhite(0.13f));
            }
            float Maximum = 6.0f;
            for (uint32_t Count : RateBuckets)
            {
                Maximum = std::max(Maximum, static_cast<float>(Count));
            }
            Maximum *= 1.25f;
            std::vector<ImVec2> Points;
            Points.reserve(RateBuckets.size());
            for (size_t Index = 0; Index < RateBuckets.size(); ++Index)
            {
                const float Px = X0 + W * static_cast<float>(Index) / static_cast<float>(RateBuckets.size() - 1);
                const float Py = Y0 + H - 3.0f - (H - 9.0f) * (static_cast<float>(RateBuckets[Index]) / Maximum);
                Points.push_back(ImVec2(Px, Py));
            }
            if (Points.size() > 1)
            {
                const ImU32 FillInk = InkRgba(47, 230, 167, 18);        // rgba(47,230,167,.07) under the line
                for (size_t Index = 1; Index < Points.size(); ++Index)
                {
                    Ink->AddQuadFilled(Points[Index - 1], Points[Index],
                                       ImVec2(Points[Index].x, Y0 + H), ImVec2(Points[Index - 1].x, Y0 + H), FillInk);
                }
                Ink->AddPolyline(Points.data(), static_cast<int>(Points.size()), InkWhite(0.85f), 0, 1.4f);
                Ink->AddCircleFilled(ImVec2(X0 + W - 2.0f, Points.back().y), 2.6f, Theme::Mint);
            }
        }
        CursorY = CardTop + CardH;
    }

    // -- Overflow menu (reusable dropdown surface) -----------------------------------------------------------------
    ImGui::SetNextWindowPos(ImVec2(CR, PT + 62.0f), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
    if (DropdownControlSurface::BeginMenu("##LogOverflowPopup"))
    {
        MenuCaption(Faces, "STREAM");
        if (DropdownControlSurface::Option("Copy visible entries"))
        {
            std::string All;
            for (const LogStreamVisibleEntry& Row : Visible)
            {
                All += PlainEntry(Row, Categories, SessionClock, SessionStartMicros);
                All += '\n';
            }
            ImGui::SetClipboardText(All.c_str());
        }
        if (DropdownControlSurface::Option("Save session log\xE2\x80\xA6"))
        {
            std::ofstream Out("FrontierLogStream.log", std::ios::trunc);
            for (const LogStreamVisibleEntry& Row : Visible)
            {
                Out << PlainEntry(Row, Categories, SessionClock, SessionStartMicros) << '\n';
            }
        }
        MenuSeparator();
        MenuCaption(Faces, "DISPLAY");
        if (DropdownControlSurface::Option("Word wrap", WrapFlag))
        {
            ConfigureWrap(!WrapFlag);
            ImGui::OpenPopup("##LogOverflowPopup");                   // Toggles keep the menu open, as prototyped
        }
        if (DropdownControlSurface::Option("Timestamps                "))
        {
            ConfigureSessionClock(!SessionClock);
            ImGui::OpenPopup("##LogOverflowPopup");
        }
        MenuTrailingText(Faces, SessionClock ? "Session" : "Wall clock", 11.0f);
        if (DropdownControlSurface::Option("Collapse repeats", CollapseFlag))
        {
            ConfigureCollapse(!CollapseFlag);
            ImGui::OpenPopup("##LogOverflowPopup");
        }
        DropdownControlSurface::EndMenu();
    }

    // -- Filter row --------------------------------------------------------------------------------------------------
    ImVec2 ChannelsTriggerMin(0, 0);
    ImVec2 ChannelsTriggerMax(0, 0);
    ImVec2 VerbosityTriggerMin(0, 0);
    ImVec2 VerbosityTriggerMax(0, 0);
    {
        const float RowTop = CursorY + 16.0f;
        const float RowCY  = RowTop + 18.5f;
        float       X      = CL;

        struct ChipSpec
        {
            const char* Label;
            LogSeverity Severity;
            bool        All;
            ImU32       Dot;                                          // 0 = no dot (All)
        };
        const ChipSpec Chips[5] = {
            {"All", LogSeverity::Info, true, 0},
            {"Info", LogSeverity::Info, false, Theme::Mint},
            {"Warnings", LogSeverity::Warning, false, Theme::Amber},
            {"Errors", LogSeverity::Error, false, Theme::Coral},
            {"Fatal", LogSeverity::Fatal, false, Theme::Red},
        };
        for (const ChipSpec& Chip : Chips)
        {
            const uint64_t    Count   = Chip.All ? TotalCount : QuerySeverityCount(Chip.Severity);
            const std::string CountText = FormatGrouped(Count);
            const float       CountW  = TextWidth(Faces.Medium, 13.5f, CountText.c_str());
            const float       LabelW  = TextWidth(Faces.Regular, 13.5f, Chip.Label);
            const float       ChipW   = 15.0f + (Chip.Dot != 0 ? 15.0f : 0.0f) + CountW + 8.0f + LabelW + 15.0f;
            const float       ChipH   = 34.0f;
            const bool        Pressed = Chip.All
                ? (SeverityEnabled[0] && SeverityEnabled[1] && SeverityEnabled[2] && SeverityEnabled[3])
                : QuerySeverityEnabled(Chip.Severity);
            const ImVec2 Minimum(X, RowCY - ChipH * 0.5f);
            bool         Hovered = false;
            ImGui::PushID(Chip.Label);
            const bool Clicked = PillTarget("##LogChip", Minimum, ImVec2(ChipW, ChipH), Hovered);
            ImGui::PopID();
            ImU32 Background = Pressed ? Theme::ChipOnBg : (Hovered ? Theme::Surface3 : Theme::Surface2);
            ImU32 InkMain    = Pressed ? Theme::ChipOnInk : (Hovered ? Theme::Text : Theme::Muted);
            ImU32 InkCount   = Pressed ? Theme::ChipOnInk : Theme::Text;
            if (!Pressed)
            {
                Background = InkFade(Background, 0.55f);
                InkMain    = InkFade(InkMain, 0.55f);
                InkCount   = InkFade(InkCount, 0.55f);
            }
            PillSurface(Ink, Minimum, ImVec2(Minimum.x + ChipW, Minimum.y + ChipH), Background, 0, 999.0f);
            float ContentX = Minimum.x + 15.0f;
            if (Chip.Dot != 0)
            {
                Ink->AddCircleFilled(ImVec2(ContentX + 3.5f, RowCY), 3.5f,
                                     Pressed ? Chip.Dot : InkFade(Chip.Dot, 0.55f));
                ContentX += 15.0f;
            }
            DrawText(Ink, Faces.Medium, 13.5f, ImVec2(ContentX, RowCY - 6.75f), InkCount, CountText.c_str());
            DrawText(Ink, Faces.Regular, 13.5f, ImVec2(ContentX + CountW + 8.0f, RowCY - 6.75f), InkMain, Chip.Label);
            if (Clicked)
            {
                if (Chip.All)
                {
                    ConfigureAllSeverities();
                }
                else
                {
                    ConfigureSeverity(Chip.Severity, !QuerySeverityEnabled(Chip.Severity));
                }
            }
            X += ChipW + 9.0f;
        }

        Ink->AddRectFilled(ImVec2(X + 3.0f, RowCY - 12.0f), ImVec2(X + 4.0f, RowCY + 12.0f), Theme::Line);
        X += 7.0f + 9.0f;

        // Dropdown triggers (menus below are the reusable DropdownControlSurface).
        std::vector<const LogCategory*> AllCategories;
        Categories.QueryAll(AllCategories);
        size_t EnabledCount = 0;
        for (const LogCategory* Category : AllCategories)
        {
            EnabledCount += QueryCategoryEnabled(Category->Token) ? 1 : 0;
        }
        char ChannelsLabel[40] = {};
        if (EnabledCount == AllCategories.size())
        {
            std::snprintf(ChannelsLabel, sizeof(ChannelsLabel), "All channels");
        }
        else if (EnabledCount == 0)
        {
            std::snprintf(ChannelsLabel, sizeof(ChannelsLabel), "No channels");
        }
        else
        {
            std::snprintf(ChannelsLabel, sizeof(ChannelsLabel), "%zu channels", EnabledCount);
        }

        const bool ChannelsOpen  = ImGui::IsPopupOpen("##LogChannelsPopup");
        const bool VerbosityOpen = ImGui::IsPopupOpen("##LogVerbosityPopup");

        {
            const float LabelW = TextWidth(Faces.Regular, 13.5f, ChannelsLabel);
            const float TrigW  = 14.0f + 15.0f + 9.0f + LabelW + 9.0f + 12.0f + 14.0f;
            const float TrigH  = 34.0f;
            ChannelsTriggerMin = ImVec2(X, RowCY - TrigH * 0.5f);
            ChannelsTriggerMax = ImVec2(X + TrigW, RowCY + TrigH * 0.5f);
            bool Hovered = false;
            if (PillTarget("##LogChannelsTrigger", ChannelsTriggerMin,
                           ImVec2(TrigW, TrigH), Hovered))
            {
                ImGui::OpenPopup("##LogChannelsPopup");
            }
            PillSurface(Ink, ChannelsTriggerMin, ChannelsTriggerMax,
                        ChannelsOpen ? Theme::Surface3 : (Hovered ? Theme::Surface3 : Theme::Surface2),
                        ChannelsOpen ? InkWhite(0.18f) : Theme::Line, 999.0f);
            DrawIconFunnel(Ink, ImVec2(ChannelsTriggerMin.x + 21.5f, RowCY), 15.0f, Theme::Muted);
            DrawText(Ink, Faces.Regular, 13.5f, ImVec2(ChannelsTriggerMin.x + 38.0f, RowCY - 6.75f), Theme::Text,
                     ChannelsLabel);
            DrawIconChevron(Ink, ImVec2(ChannelsTriggerMax.x - 20.0f, RowCY), 12.0f, Theme::Muted, ChannelsOpen);
            X += TrigW + 9.0f;
        }
        {
            const char* Value  = CeilingText(Ceiling);
            const float DimW   = TextWidth(Faces.Regular, 13.5f, "Verbosity");
            const float ValueW = TextWidth(Faces.Regular, 13.5f, Value);
            const float TrigW  = 14.0f + DimW + 9.0f + ValueW + 9.0f + 12.0f + 14.0f;
            const float TrigH  = 34.0f;
            VerbosityTriggerMin = ImVec2(X, RowCY - TrigH * 0.5f);
            VerbosityTriggerMax = ImVec2(X + TrigW, RowCY + TrigH * 0.5f);
            bool Hovered = false;
            if (PillTarget("##LogVerbosityTrigger", VerbosityTriggerMin,
                           ImVec2(TrigW, TrigH), Hovered))
            {
                ImGui::OpenPopup("##LogVerbosityPopup");
            }
            PillSurface(Ink, VerbosityTriggerMin, VerbosityTriggerMax,
                        VerbosityOpen ? Theme::Surface3 : (Hovered ? Theme::Surface3 : Theme::Surface2),
                        VerbosityOpen ? InkWhite(0.18f) : Theme::Line, 999.0f);
            DrawText(Ink, Faces.Regular, 13.5f, ImVec2(VerbosityTriggerMin.x + 14.0f, RowCY - 6.75f), Theme::Faint,
                     "Verbosity");
            DrawText(Ink, Faces.Regular, 13.5f,
                     ImVec2(VerbosityTriggerMin.x + 14.0f + DimW + 9.0f, RowCY - 6.75f), Theme::Text, Value);
            DrawIconChevron(Ink, ImVec2(VerbosityTriggerMax.x - 20.0f, RowCY), 12.0f, Theme::Muted, VerbosityOpen);
            X += TrigW + 9.0f;
        }

        // Follow pill (right edge), measured before the flexing search field.
        const char* FollowText = FollowFlag ? "Following" : "Follow";
        const float FollowW    = 14.0f + 7.0f + 8.0f + TextWidth(Faces.Regular, 13.0f, FollowText) + 14.0f;
        const float FollowX    = CR - FollowW;

        // Search pill with match count and clear control.
        {
            const float SearchW = std::min(330.0f, std::max(140.0f, FollowX - 9.0f - X));
            const ImVec2 Minimum(X, RowTop);
            const ImVec2 Maximum(X + SearchW, RowTop + 37.0f);
            PillSurface(Ink, Minimum, Maximum, Theme::Surface2,
                        SearchFocused ? InkWithAlpha(Theme::Mint, 0.45f) : Theme::Line, 999.0f);
            DrawIconSearch(Ink, ImVec2(X + 20.5f, RowCY), 15.0f, Theme::Faint);

            const bool HasText = !Query.empty();
            char Shown[40] = {};
            float TrailingW = 8.0f;
            if (HasText)
            {
                std::snprintf(Shown, sizeof(Shown), "%zu shown", Visible.size());
                TrailingW = TextWidth(Faces.Regular, 11.5f, Shown) + 8.0f + 24.0f + 8.0f;
            }
            if (Query != SearchSynchronised)
            {
                std::snprintf(SearchBuffer.data(), SearchBuffer.size(), "%s", Query.c_str());
                SearchSynchronised = Query;
            }
            ImGui::SetCursorScreenPos(ImVec2(X + 36.0f, RowTop + (37.0f - 13.5f) * 0.5f));
            ImGui::PushFont(Faces.Regular, 13.5f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.933f, 0.953f, 0.941f, 1.0f));
            ImGui::SetNextItemWidth(std::max(30.0f, SearchW - 36.0f - TrailingW));
            if (ImGui::InputTextWithHint("##LogSearch", "Filter text\xE2\x80\xA6", SearchBuffer.data(),
                                         SearchBuffer.size()))
            {
                ConfigureQuery(SearchBuffer.data());
                SearchSynchronised = Query;
            }
            SearchFocused = ImGui::IsItemActive();
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar(2);
            ImGui::PopFont();

            if (HasText)
            {
                const float ShownW = TextWidth(Faces.Regular, 11.5f, Shown);
                DrawText(Ink, Faces.Regular, 11.5f, ImVec2(Maximum.x - 8.0f - 24.0f - 8.0f - ShownW, RowCY - 5.75f),
                         Theme::Faint, Shown);
                const ImVec2 DiscCentre(Maximum.x - 8.0f - 12.0f, RowCY);
                bool ClearHover = false;
                if (PillTarget("##LogSearchClear", ImVec2(DiscCentre.x - 12.0f, DiscCentre.y - 12.0f),
                               ImVec2(24.0f, 24.0f), ClearHover, "Clear filter"))
                {
                    SearchBuffer[0] = '\0';
                    ConfigureQuery("");
                    SearchSynchronised = Query;
                }
                Ink->AddCircleFilled(DiscCentre, 12.0f, Theme::Surface3);
                DrawIconCross(Ink, DiscCentre, 11.0f, ClearHover ? Theme::Text : Theme::Muted);
            }
        }

        {
            const float  FollowH = 34.0f;
            const ImVec2 Minimum(FollowX, RowCY - FollowH * 0.5f);
            bool Hovered = false;
            if (PillTarget("##LogFollow", Minimum, ImVec2(FollowW, FollowH), Hovered, "Follow the newest entries"))
            {
                ConfigureFollow(!FollowFlag);
            }
            PillSurface(Ink, Minimum, ImVec2(Minimum.x + FollowW, Minimum.y + FollowH),
                        FollowFlag ? Theme::MintDim : 0,
                        FollowFlag ? InkWithAlpha(Theme::Mint, 0.4f) : Theme::Line, 999.0f);
            Ink->AddCircleFilled(ImVec2(Minimum.x + 17.5f, RowCY), 3.5f, FollowFlag ? Theme::Mint : Theme::Faint);
            DrawText(Ink, Faces.Regular, 13.0f, ImVec2(Minimum.x + 29.0f, RowCY - 6.5f),
                     FollowFlag ? Theme::Mint : Theme::Faint, FollowText);
        }
        CursorY = RowTop + 37.0f + 12.0f;
    }

    // -- Channels menu (reusable dropdown surface) -----------------------------------------------------------------
    ImGui::SetNextWindowPos(ImVec2(ChannelsTriggerMin.x, ChannelsTriggerMax.y + 8.0f), ImGuiCond_Appearing);
    if (DropdownControlSurface::BeginMenu("##LogChannelsPopup"))
    {
        std::vector<const LogCategory*> All;
        Categories.QueryAll(All);
        const auto DrawCategory = [this, &Faces](const LogCategory* Category) {
            char Caption[96] = {};
            std::snprintf(Caption, sizeof(Caption), Category->UserCreated ? "   %s" : "%s", Category->Name.c_str());
            if (DropdownControlSurface::Option(Caption, QueryCategoryEnabled(Category->Token)))
            {
                ConfigureCategory(Category->Token, !QueryCategoryEnabled(Category->Token));
                ImGui::OpenPopup("##LogChannelsPopup");               // Checklist stays open, as prototyped
            }
            if (Category->UserCreated)
            {
                ImDrawList*  MenuInk  = ImGui::GetWindowDrawList();
                const ImVec2 Minimum  = ImGui::GetItemRectMin();
                const ImVec2 Maximum  = ImGui::GetItemRectMax();
                const ImU32  DotInk   = ImGui::GetColorU32(ToImVec4(ResolveLogColour(Category->Colour)));
                MenuInk->AddCircleFilled(ImVec2(Minimum.x + 15.5f, (Minimum.y + Maximum.y) * 0.5f), 3.5f, DotInk);
            }
            const std::string Count = FormatGrouped(QueryCategoryCount(Category->Token));
            MenuTrailingText(Faces, Count.c_str(), 12.0f);
        };

        MenuCaption(Faces, "ENGINE CHANNELS");
        for (const LogCategory* Category : All)
        {
            if (!Category->UserCreated)
            {
                DrawCategory(Category);
            }
        }
        MenuCaption(Faces, "USER CATEGORIES");
        bool AnyUser = false;
        for (const LogCategory* Category : All)
        {
            if (Category->UserCreated)
            {
                AnyUser = true;
                DrawCategory(Category);
            }
        }
        if (!AnyUser)
        {
            ImDrawList*  MenuInk = ImGui::GetWindowDrawList();
            const ImVec2 At      = ImGui::GetCursorScreenPos();
            DrawText(MenuInk, Faces.Regular, 12.0f, ImVec2(At.x + 10.0f, At.y + 2.0f), Theme::Faint,
                     "None yet \xC2\xB7 create one below");
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 20.0f));
        }

        // Category creator (.cat-create).
        {
            const float Width = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f);
            ImGui::SetNextItemWidth(Width - 20.0f);
            ImGui::InputTextWithHint("##NewCategory", "New category name\xE2\x80\xA6", CategoryNameBuffer.data(),
                                     CategoryNameBuffer.size());
            ImGui::PopStyleVar(2);

            const LogColour Swatches[5] = {LogColour::Blue, LogColour::Purple, LogColour::Pink, LogColour::Mint,
                                           LogColour::Amber};
            const ImVec2 SwatchOrigin = ImGui::GetCursorScreenPos();
            float        SwatchX      = SwatchOrigin.x + 10.0f;
            const float  SwatchY      = SwatchOrigin.y + 8.0f;
            for (LogColour Swatch : Swatches)
            {
                ImGui::SetCursorScreenPos(ImVec2(SwatchX, SwatchY));
                ImGui::PushID(static_cast<int>(Swatch));
                if (ImGui::InvisibleButton("##LogSwatch", ImVec2(18.0f, 18.0f)))
                {
                    PendingColour = Swatch;
                }
                ImGui::PopID();
                ImDrawList* MenuInk = ImGui::GetWindowDrawList();
                const ImU32 Fill    = ImGui::GetColorU32(ToImVec4(ResolveLogColour(Swatch)));
                const ImVec2 Centre(SwatchX + 9.0f, SwatchY + 9.0f);
                MenuInk->AddCircleFilled(Centre, PendingColour == Swatch ? 8.0f : 7.0f, Fill);
                MenuInk->AddCircle(Centre, 9.0f,
                                   PendingColour == Swatch ? InkWhite(1.0f) : InkWhite(0.14f), 24,
                                   PendingColour == Swatch ? 2.0f : 1.0f);
                SwatchX += 25.0f;
            }
            ImGui::SetCursorScreenPos(ImVec2(SwatchOrigin.x + 10.0f, SwatchY + 26.0f));
            const bool AddClicked = ImGui::InvisibleButton("##LogAddCategory", ImVec2(Width - 20.0f, 30.0f));
            const bool AddHovered = ImGui::IsItemHovered();
            {
                ImDrawList* MenuInk = ImGui::GetWindowDrawList();
                const ImVec2 Minimum = ImGui::GetItemRectMin();
                const ImVec2 Maximum = ImGui::GetItemRectMax();
                MenuInk->AddRectFilled(Minimum, Maximum,
                                       AddHovered ? InkWithAlpha(Theme::Mint, 0.22f) : Theme::MintDim, 9.0f);
                const float TextW = TextWidth(Faces.Medium, 12.5f, "Add category");
                DrawText(MenuInk, Faces.Medium, 12.5f,
                         ImVec2((Minimum.x + Maximum.x - TextW) * 0.5f, Minimum.y + (30.0f - 12.5f) * 0.5f),
                         Theme::Mint, "Add category");
            }
            if (AddClicked)
            {
                const LogCategory Created = RegisterLogCategory(CategoryNameBuffer.data(), PendingColour);
                if (Created.Token != 0)
                {
                    CategoryNameBuffer[0] = '\0';
                    WriteLog(Created.Token, LogSeverity::Info, LogVerbosity::Display,
                             "Category registered by user \xC2\xB7 now receiving records",
                             "LogStreamPanelPresentation.cpp");
                }
            }
        }

        MenuSeparator();
        {
            const float  Width  = ImGui::GetContentRegionAvail().x;
            const ImVec2 Origin = ImGui::GetCursorScreenPos();
            const float  EachW  = (Width - 8.0f - 12.0f) / 3.0f;       // [.menu-quick] - 4px sides, 6px gaps
            const char*  Labels[3] = {"All", "None", "Invert"};
            for (int Index = 0; Index < 3; ++Index)
            {
                const ImVec2 Minimum(Origin.x + 4.0f + static_cast<float>(Index) * (EachW + 6.0f), Origin.y + 2.0f);
                ImGui::SetCursorScreenPos(Minimum);
                ImGui::PushID(Labels[Index]);
                const bool Clicked = ImGui::InvisibleButton("##LogQuick", ImVec2(EachW, 27.0f));
                const bool Hovered = ImGui::IsItemHovered();
                ImGui::PopID();
                ImDrawList* MenuInk = ImGui::GetWindowDrawList();
                MenuInk->AddRectFilled(Minimum, ImVec2(Minimum.x + EachW, Minimum.y + 27.0f),
                                       Hovered ? InkHex(0x243129) : Theme::Surface3, 8.0f);
                const float TextW = TextWidth(Faces.Regular, 12.0f, Labels[Index]);
                DrawText(MenuInk, Faces.Regular, 12.0f,
                         ImVec2(Minimum.x + (EachW - TextW) * 0.5f, Minimum.y + 7.5f),
                         Hovered ? Theme::Text : Theme::Muted, Labels[Index]);
                if (Clicked)
                {
                    if (Index == 0)
                    {
                        ConfigureAllCategories(Categories, true);
                    }
                    else if (Index == 1)
                    {
                        ConfigureAllCategories(Categories, false);
                    }
                    else
                    {
                        InvertCategories(Categories);
                    }
                }
            }
            ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 33.0f));
            ImGui::Dummy(ImVec2(Width, 1.0f));
        }
        DropdownControlSurface::EndMenu();
    }

    // -- Verbosity menu (reusable dropdown surface) ------------------------------------------------------------------
    ImGui::SetNextWindowPos(ImVec2(VerbosityTriggerMin.x, VerbosityTriggerMax.y + 8.0f), ImGuiCond_Appearing);
    if (DropdownControlSurface::BeginMenu("##LogVerbosityPopup"))
    {
        MenuCaption(Faces, "SHOW UP TO");
        const LogCeiling Options[6] = {LogCeiling::FatalOnly, LogCeiling::Error, LogCeiling::Warning,
                                       LogCeiling::Display, LogCeiling::Log, LogCeiling::Verbose};
        for (LogCeiling Option : Options)
        {
            if (DropdownControlSurface::Option(CeilingText(Option), Option == Ceiling))
            {
                ConfigureCeiling(Option);
            }
        }
        DropdownControlSurface::EndMenu();
    }

    // -- Column labels -----------------------------------------------------------------------------------------------
    const float RowInnerL = PL + 24.0f;                              // [px] - Stream 12 + row padding 12
    const float TimeX     = Narrow ? RowInnerL + 34.0f : RowInnerL + 34.0f;
    const float TagX      = TimeX + 92.0f + 12.0f;
    const float MessageX  = Narrow ? TimeX + 84.0f + 12.0f : TagX + 108.0f + 12.0f;
    if (!Narrow)
    {
        const float LabelsY = CursorY;
        DrawTracked(Ink, Faces.Medium, 10.5f, ImVec2(TimeX, LabelsY), Theme::Faint, "TIME", 10.5f * 0.13f);
        DrawTracked(Ink, Faces.Medium, 10.5f, ImVec2(TagX, LabelsY), Theme::Faint, "CHANNEL", 10.5f * 0.13f);
        DrawTracked(Ink, Faces.Medium, 10.5f, ImVec2(MessageX, LabelsY), Theme::Faint, "MESSAGE", 10.5f * 0.13f);
        const float SourceW = TrackedWidth(Faces.Medium, 10.5f, "SOURCE", 10.5f * 0.13f);
        DrawTracked(Ink, Faces.Medium, 10.5f, ImVec2(PR - 22.0f - SourceW, LabelsY), Theme::Faint, "SOURCE",
                    10.5f * 0.13f);
        CursorY += 13.0f + 7.0f;
    }

    // -- Stream ------------------------------------------------------------------------------------------------------
    const float FooterH   = 51.0f;                                   // [px] - 11 + 26 + 14
    const float StreamTop = CursorY;
    const float StreamH   = std::max(30.0f, (PB - FooterH) - StreamTop);
    Ink->AddLine(ImVec2(PL, StreamTop), ImVec2(PR, StreamTop), Theme::LineSoft);

    ImGui::SetCursorScreenPos(ImVec2(PL, StreamTop + 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::BeginChild("##LogRows", ImVec2(WindowSize.x, StreamH - 1.0f), false))
    {
        ImDrawList* Rows = ImGui::GetWindowDrawList();
        const float ChildW = ImGui::GetWindowWidth();

        const bool AtEnd = ScrollPrimed && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f;
        if (ScrollPrimed && FollowFlag && !AtEnd && ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
        {
            NotifyScrolledUp();
        }
        if (AtEnd)
        {
            NotifyScrolledToEnd();
        }

        ImGui::Dummy(ImVec2(ChildW, 6.0f));

        size_t RowIndex = 0;
        for (const LogStreamVisibleEntry& Row : Visible)
        {
            const LogCategory* Category = Categories.Query(Row.Entry.Category);
            const bool         Selected = Row.Entry.Sequence == SelectedSequence;

            const float SourceWdt = TextWidth(Faces.Regular, 11.5f, Row.Entry.Source);
            float MetaLeftInset = 12.0f + SourceWdt;                  // [px] - From the row's right edge
            char BadgeText[16] = {};
            float BadgeWidth = 0.0f;
            if (Row.RepeatCount > 1)
            {
                std::snprintf(BadgeText, sizeof(BadgeText), "\xC3\x97%u", Row.RepeatCount);
                BadgeWidth = TextWidth(Faces.Medium, 11.0f, BadgeText) + 16.0f;
                MetaLeftInset += BadgeWidth + 9.0f;
            }
            const float RowW      = ChildW - 22.0f;                   // [px] - List padding 12 left, 10 right
            const float MessageW  = RowW - (MessageX - (PL + 12.0f)) - MetaLeftInset - 12.0f;

            std::vector<TextSpan> MessageLines;
            std::vector<TextSpan> Matches;
            std::string           Ellipsised;
            float RowH = 35.0f;
            if (WrapFlag)
            {
                MessageLines = WrapTextLines(Faces.Regular, 13.5f, Row.Entry.Message, std::max(20.0f, MessageW));
                Matches      = FindMatches(Row.Entry.Message, QueryLower);
                const float TextH = static_cast<float>(MessageLines.size()) * 19.575f; // 13.5 * 1.45
                RowH = 14.0f + std::max(21.0f, TextH);
            }
            else
            {
                Ellipsised = EllipsizeText(Faces.Regular, 13.5f, Row.Entry.Message, std::max(20.0f, MessageW));
                Matches    = FindMatches(Ellipsised.c_str(), QueryLower);
            }

            float DetailH = 0.0f;
            std::vector<TextSpan> DetailLines;
            float DetailTextH = 0.0f;
            if (Selected)
            {
                const float DetailTextW = RowW - 46.0f - 28.0f;       // [px] - 12 row + 34 margin, 14 card pads
                DetailLines  = WrapTextLines(Faces.Regular, 13.0f, Row.Entry.Message, std::max(20.0f, DetailTextW));
                DetailTextH  = static_cast<float>(DetailLines.size()) * 20.15f;        // 13 * 1.55
                DetailH      = 4.0f + (12.0f + DetailTextH + 9.0f + 14.0f + 11.0f + 26.0f + 12.0f) + 6.0f;
            }
            const float TotalH = RowH + DetailH;

            ImGui::SetCursorPosX(12.0f);
            ImGui::PushID(static_cast<int>(Row.Entry.Sequence & 0x7fffffff));
            const bool Clicked = ImGui::InvisibleButton("##LogRow", ImVec2(RowW, TotalH));
            const bool Hovered = ImGui::IsItemHovered();
            if (Clicked)
            {
                SelectVisible(RowIndex);
            }
            const ImVec2 Minimum = ImGui::GetItemRectMin();
            const ImVec2 Maximum = ImGui::GetItemRectMax();

            if (RowIndex > 0)
            {
                Rows->AddLine(ImVec2(Minimum.x, Minimum.y), ImVec2(Maximum.x, Minimum.y), Theme::RowSep);
            }
            const bool Alert = Row.Entry.Severity == LogSeverity::Error || Row.Entry.Severity == LogSeverity::Fatal;
            if (Row.Entry.Severity == LogSeverity::Fatal)
            {
                Rows->AddRectFilled(Minimum, Maximum, InkWithAlpha(Theme::Red, 0.055f), 11.0f);
            }
            else if (Selected)
            {
                Rows->AddRectFilled(Minimum, Maximum, InkWithAlpha(Theme::Mint, 0.055f), 11.0f);
            }
            else if (Hovered)
            {
                Rows->AddRectFilled(Minimum, Maximum, Theme::RowHover, 11.0f);
            }
            if (Alert)
            {
                Rows->AddRectFilled(ImVec2(Minimum.x, Minimum.y + 8.0f),
                                    ImVec2(Minimum.x + 3.0f, Maximum.y - 8.0f), Theme::Coral, 1.5f);
            }
            else if (Selected)
            {
                Rows->AddRectFilled(ImVec2(Minimum.x, Minimum.y + 8.0f),
                                    ImVec2(Minimum.x + 3.0f, Maximum.y - 8.0f), Theme::Mint, 1.5f);
            }

            const float RowCY = Minimum.y + RowH * 0.5f;
            const float InnerX = Minimum.x + 12.0f;

            // Severity glyph.
            const ImVec2 GlyphCentre(InnerX + 11.0f, RowCY);
            switch (Row.Entry.Severity)
            {
                case LogSeverity::Warning:
                    DrawWarnGlyph(Rows, GlyphCentre, 15.0f, Theme::Amber);
                    break;
                case LogSeverity::Error:
                case LogSeverity::Fatal:
                    DrawWarnGlyph(Rows, GlyphCentre, 15.0f, Theme::Coral);
                    break;
                case LogSeverity::Info:
                    Rows->AddCircleFilled(GlyphCentre, 3.5f, InkWithAlpha(Theme::Mint, 0.85f));
                    break;
            }

            // Time.
            const LogClockText Clock = FormatLogClock(Row.Entry.WallMicros, SessionStartMicros, SessionClock);
            const float TimeColX = InnerX + 34.0f;
            DrawText(Rows, Faces.Regular, 12.5f, ImVec2(TimeColX, RowCY - 6.25f), Theme::Muted, Clock.Main);
            DrawText(Rows, Faces.Regular, 12.5f,
                     ImVec2(TimeColX + TextWidth(Faces.Regular, 12.5f, Clock.Main), RowCY - 6.25f), Theme::MsRow,
                     Clock.Millis);

            // Channel tag (hidden below 900px, per the prototype's media query).
            if (!Narrow)
            {
                std::string TagName = Category != nullptr ? Category->Name : "Unknown";
                std::transform(TagName.begin(), TagName.end(), TagName.begin(), [](unsigned char Glyph) {
                    return static_cast<char>(std::toupper(Glyph));
                });
                const bool  IsUser = Category != nullptr && Category->UserCreated;
                const ImU32 CatInk = IsUser ? ImGui::GetColorU32(ToImVec4(ResolveLogColour(Category->Colour)))
                                            : Theme::Muted;
                const float TagTextW = TrackedWidth(Faces.Medium, 10.5f, TagName.c_str(), 10.5f * 0.09f);
                const float TagW = 9.0f + (IsUser ? 12.0f : 0.0f) + TagTextW + 9.0f;
                const ImVec2 TagMin(TagX - (PL + 12.0f) + Minimum.x, RowCY - 10.5f);
                PillSurface(Rows, TagMin, ImVec2(TagMin.x + TagW, TagMin.y + 21.0f),
                            IsUser ? InkWithAlpha(CatInk, 0.09f) : InkWhite(0.02f),
                            IsUser ? InkWithAlpha(CatInk, 0.42f) : Theme::Line, 999.0f);
                float TagTextX = TagMin.x + 9.0f;
                if (IsUser)
                {
                    Rows->AddCircleFilled(ImVec2(TagTextX + 3.0f, RowCY), 3.0f, CatInk);
                    TagTextX += 12.0f;
                }
                DrawTracked(Rows, Faces.Medium, 10.5f, ImVec2(TagTextX, RowCY - 5.25f), CatInk, TagName.c_str(),
                            10.5f * 0.09f);
            }

            // Message.
            const ImU32 MessageInk = Row.Entry.Severity == LogSeverity::Warning ? Theme::MsgWarn
                                   : Alert ? Theme::MsgErr
                                           : Theme::MsgText;
            const float MsgX = MessageX - (PL + 12.0f) + Minimum.x;
            if (WrapFlag)
            {
                float LineY = Minimum.y + 7.0f + (std::max(21.0f, static_cast<float>(MessageLines.size()) * 19.575f) -
                                                 static_cast<float>(MessageLines.size()) * 19.575f) * 0.5f;
                for (const TextSpan& Line : MessageLines)
                {
                    DrawMessageLine(Rows, Faces.Regular, 13.5f, ImVec2(MsgX, LineY), MessageInk, Line.Begin,
                                    Line.End, Matches);
                    LineY += 19.575f;
                }
            }
            else
            {
                DrawMessageLine(Rows, Faces.Regular, 13.5f, ImVec2(MsgX, RowCY - 6.75f), MessageInk,
                                Ellipsised.c_str(), Ellipsised.c_str() + Ellipsised.size(), Matches);
            }

            // Source + repeat badge.
            DrawText(Rows, Faces.Regular, 11.5f, ImVec2(Maximum.x - 12.0f - SourceWdt, RowCY - 5.75f),
                     Theme::Faint, Row.Entry.Source);
            if (BadgeWidth > 0.0f)
            {
                const ImVec2 BadgeMin(Maximum.x - 12.0f - SourceWdt - 9.0f - BadgeWidth, RowCY - 8.5f);
                Rows->AddRectFilled(BadgeMin, ImVec2(BadgeMin.x + BadgeWidth, BadgeMin.y + 17.0f), Theme::Surface3,
                                    999.0f);
                DrawText(Rows, Faces.Medium, 11.0f, ImVec2(BadgeMin.x + 8.0f, RowCY - 5.5f), Theme::Muted, BadgeText);
            }

            // Expanded detail card.
            if (Selected)
            {
                const ImVec2 CardMin(Minimum.x + 46.0f, Minimum.y + RowH + 4.0f);
                const ImVec2 CardMax(Maximum.x - 12.0f, CardMin.y + (DetailH - 10.0f));
                Rows->AddRectFilled(CardMin, CardMax, Theme::Surface, 12.0f);
                Rows->AddRect(CardMin, CardMax, Theme::LineSoft, 12.0f);

                float LineY = CardMin.y + 12.0f;
                for (const TextSpan& Line : DetailLines)
                {
                    Rows->AddText(Faces.Regular, 13.0f, ImVec2(CardMin.x + 14.0f, LineY), Theme::DetailTx, Line.Begin,
                                  Line.End);
                    LineY += 20.15f;
                }

                const LogClockText First = FormatLogClock(Row.FirstWallMicros, SessionStartMicros, SessionClock);
                const LogClockText Last  = FormatLogClock(Row.Entry.WallMicros, SessionStartMicros, SessionClock);
                char Occurrences[16] = {};
                std::snprintf(Occurrences, sizeof(Occurrences), "%u", Row.RepeatCount);
                std::string FirstText = std::string(First.Main) + First.Millis;
                std::string LastText  = std::string(Last.Main) + Last.Millis;
                const std::pair<const char*, const char*> Meta[6] = {
                    {"Severity", SeverityDetailText(Row.Entry.Severity)},
                    {"Verbosity", VerbosityDetailText(Row.Entry.Verbosity)},
                    {"First", FirstText.c_str()},
                    {"Last", LastText.c_str()},
                    {"Occurrences", Occurrences},
                    {"Source", Row.Entry.Source},
                };
                float MetaX = CardMin.x + 14.0f;
                const float MetaY = CardMin.y + 12.0f + DetailTextH + 9.0f;
                for (const auto& Pair : Meta)
                {
                    DrawText(Rows, Faces.Regular, 11.5f, ImVec2(MetaX, MetaY), Theme::Faint, Pair.first);
                    MetaX += TextWidth(Faces.Regular, 11.5f, Pair.first) + 4.0f;
                    DrawText(Rows, Faces.Medium, 11.5f, ImVec2(MetaX, MetaY), Theme::Muted, Pair.second);
                    MetaX += TextWidth(Faces.Medium, 11.5f, Pair.second) + 16.0f;
                }

                const float ActionsY = MetaY + 14.0f + 11.0f;
                float ActionX = CardMin.x + 14.0f;
                const char* ActionLabels[3] = {"Copy entry", "Only this channel", "Filter to text"};
                for (int ActionIndex = 0; ActionIndex < 3; ++ActionIndex)
                {
                    const float ActionW = TextWidth(Faces.Regular, 12.0f, ActionLabels[ActionIndex]) + 24.0f;
                    bool ActionHover = false;
                    ImGui::PushID(ActionLabels[ActionIndex]);
                    const bool ActionClicked = PillTarget("##LogDetailAction", ImVec2(ActionX, ActionsY),
                                                          ImVec2(ActionW, 26.0f), ActionHover);
                    ImGui::PopID();
                    PillSurface(Rows, ImVec2(ActionX, ActionsY), ImVec2(ActionX + ActionW, ActionsY + 26.0f),
                                ActionHover ? Theme::Surface3 : 0, Theme::Line, 999.0f);
                    DrawText(Rows, Faces.Regular, 12.0f, ImVec2(ActionX + 12.0f, ActionsY + 7.0f),
                             ActionHover ? Theme::Text : Theme::Muted, ActionLabels[ActionIndex]);
                    if (ActionClicked)
                    {
                        if (ActionIndex == 0)
                        {
                            ImGui::SetClipboardText(
                                PlainEntry(Row, Categories, SessionClock, SessionStartMicros).c_str());
                        }
                        else if (ActionIndex == 1)
                        {
                            ConfigureAllCategories(Categories, false);
                            ConfigureCategory(Row.Entry.Category, true);
                        }
                        else
                        {
                            std::string Message = Row.Entry.Message;
                            const size_t Split  = Message.find(" \xC2\xB7 ");
                            ConfigureQuery(Split == std::string::npos ? Message : Message.substr(0, Split));
                        }
                    }
                    ActionX += ActionW + 7.0f;
                }
            }
            ImGui::PopID();
            RowIndex += 1;
        }

        if (Visible.empty())
        {
            const char* Headline = "No entries match";
            const char* Guidance = "Loosen the severity chips, channels or filter text.";
            const float HeadW = TextWidth(Faces.Medium, 17.0f, Headline);
            const float GuideW = TextWidth(Faces.Regular, 14.0f, Guidance);
            const ImVec2 At = ImGui::GetCursorScreenPos();
            DrawText(Rows, Faces.Medium, 17.0f, ImVec2(At.x + (ChildW - HeadW) * 0.5f, At.y + 52.0f), Theme::Muted,
                     Headline);
            DrawText(Rows, Faces.Regular, 14.0f, ImVec2(At.x + (ChildW - GuideW) * 0.5f, At.y + 52.0f + 25.0f),
                     Theme::Faint, Guidance);
            ImGui::Dummy(ImVec2(ChildW, 52.0f + 25.0f + 18.0f + 52.0f));
        }
        else
        {
            ImGui::Dummy(ImVec2(ChildW, 14.0f));
        }

        if (FollowFlag)
        {
            ImGui::SetScrollHereY(1.0f);
        }
        ScrollPrimed = true;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // -- New-entries pill (floating over the stream) ------------------------------------------------------------------
    if (NewBelow > 0)
    {
        char PillText[32] = {};
        std::snprintf(PillText, sizeof(PillText), "%u new", NewBelow);
        const float PillW = 16.0f + 13.0f + 8.0f + TextWidth(Faces.Medium, 13.0f, PillText) + 16.0f;
        const float PillH = 33.0f;
        const ImVec2 Minimum((PL + PR - PillW) * 0.5f, StreamTop + StreamH - 16.0f - PillH);
        bool Hovered = false;
        if (PillTarget("##LogNewPill", Minimum, ImVec2(PillW, PillH), Hovered))
        {
            JumpToLatest();
        }
        for (int Layer = 3; Layer >= 1; --Layer)
        {
            const float Grow = static_cast<float>(Layer) * 6.0f;
            Ink->AddRectFilled(ImVec2(Minimum.x - Grow, Minimum.y - Grow + Grow * 0.55f),
                               ImVec2(Minimum.x + PillW + Grow, Minimum.y + PillH + Grow * 0.55f),
                               InkWithAlpha(Theme::Mint, 0.35f / (static_cast<float>(Layer) + 1.0f)), 999.0f);
        }
        PillSurface(Ink, Minimum, ImVec2(Minimum.x + PillW, Minimum.y + PillH), Theme::Mint, 0, 999.0f);
        DrawIconArrowDown(Ink, ImVec2(Minimum.x + 22.5f, Minimum.y + PillH * 0.5f), 13.0f, Theme::MintDeep);
        DrawText(Ink, Faces.Medium, 13.0f, ImVec2(Minimum.x + 37.0f, Minimum.y + (PillH - 13.0f) * 0.5f),
                 Theme::MintDeep, PillText);
    }

    // -- Footer -------------------------------------------------------------------------------------------------------
    {
        const float FooterTop = PB - FooterH;
        Ink->AddLine(ImVec2(PL, FooterTop), ImVec2(PR, FooterTop), Theme::LineSoft);
        const float StatY = FooterTop + 11.0f + (26.0f - 12.5f) * 0.5f;

        const std::string Shown = FormatGrouped(static_cast<uint64_t>(Visible.size()));
        const std::string Total = FormatGrouped(TotalCount);
        float X = CL;
        DrawText(Ink, Faces.Medium, 12.5f, ImVec2(X, StatY), Theme::Muted, Shown.c_str());
        X += TextWidth(Faces.Medium, 12.5f, Shown.c_str());
        DrawText(Ink, Faces.Regular, 12.5f, ImVec2(X, StatY), Theme::Faint, " shown \xC2\xB7 ");
        X += TextWidth(Faces.Regular, 12.5f, " shown \xC2\xB7 ");
        DrawText(Ink, Faces.Medium, 12.5f, ImVec2(X, StatY), Theme::Muted, Total.c_str());
        X += TextWidth(Faces.Medium, 12.5f, Total.c_str());
        DrawText(Ink, Faces.Regular, 12.5f, ImVec2(X, StatY), Theme::Faint, " total");
        X += TextWidth(Faces.Regular, 12.5f, " total");
        const float ScrubLeft = X + 18.0f;

        const std::string Dropped = FormatGrouped(DroppedCount);
        char SessionText[16] = "00:00";
        if (SessionStartMicros > 0)
        {
            const uint64_t ElapsedSeconds = (NowWallMicros() - SessionStartMicros) / 1000000ull;
            std::snprintf(SessionText, sizeof(SessionText), "%02llu:%02llu",
                          static_cast<unsigned long long>(ElapsedSeconds / 60ull),
                          static_cast<unsigned long long>(ElapsedSeconds % 60ull));
        }
        char RingText[16] = {};
        std::snprintf(RingText, sizeof(RingText), "%uk", LogRingStore::Capacity / 1024u);
        const std::pair<const char*, const char*> RightStats[3] = {
            {"Ring ", RingText}, {"Dropped ", Dropped.c_str()}, {"Session ", SessionText}};
        float RightW = 0.0f;
        for (const auto& Stat : RightStats)
        {
            RightW += TextWidth(Faces.Regular, 12.5f, Stat.first) + TextWidth(Faces.Medium, 12.5f, Stat.second) + 16.0f;
        }
        RightW -= 16.0f;
        float RightX = CR - RightW;
        for (const auto& Stat : RightStats)
        {
            DrawText(Ink, Faces.Regular, 12.5f, ImVec2(RightX, StatY), Theme::Faint, Stat.first);
            RightX += TextWidth(Faces.Regular, 12.5f, Stat.first);
            DrawText(Ink, Faces.Medium, 12.5f, ImVec2(RightX, StatY), Theme::Muted, Stat.second);
            RightX += TextWidth(Faces.Medium, 12.5f, Stat.second) + 16.0f;
        }

        // Session scrubber: exact port of the prototype's canvas routine.
        const float SX = ScrubLeft;
        const float SY = FooterTop + 11.0f;
        const float SW = (CR - RightW - 18.0f) - SX;
        const float SH = 26.0f;
        if (SW > 20.0f)
        {
            Ink->AddLine(ImVec2(SX, SY + SH * 0.5f), ImVec2(SX + SW, SY + SH * 0.5f), InkWhite(0.1f));
            const float TickW = std::max(2.0f, SW / 160.0f);
            for (size_t Index = 0; Index < ScrubBuckets.size(); ++Index)
            {
                const LogScrubBucket& Bucket = ScrubBuckets[Index];
                const float TickX = SX + SW - static_cast<float>(ScrubBuckets.size() - Index) * TickW;
                if (TickX < SX)
                {
                    continue;
                }
                const float TickH = 4.0f + std::min(9.0f, static_cast<float>(Bucket.Count));
                const ImU32 TickInk = Bucket.WorstSeverity >= 2 ? InkWithAlpha(Theme::Coral, 0.9f)
                                    : Bucket.WorstSeverity == 1 ? InkWithAlpha(Theme::Amber, 0.85f)
                                                                : InkWhite(0.22f);
                Ink->AddRectFilled(ImVec2(TickX, SY + (SH - TickH) * 0.5f),
                                   ImVec2(TickX + TickW - 1.0f, SY + (SH + TickH) * 0.5f), TickInk);
            }
            Ink->AddRectFilled(ImVec2(SX + SW - 2.0f, SY + 3.0f), ImVec2(SX + SW, SY + SH - 3.0f), Theme::Mint);
        }
    }
}

} // namespace Frontier

#else // FRONTIER_WITH_INTERFACE_SDK

namespace Frontier
{

void LogStreamPanel::ConstructInterface()
{
    // No interface SDK in this build: the panel logic runs (and is validated) headlessly; drawing arrives
    //    with the SDK-backed build. Nothing is faked here.
}

} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK

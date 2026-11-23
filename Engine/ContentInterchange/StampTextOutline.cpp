/*============================================================================================================================================
                                                          STAMPTEXTOUTLINE.CPP
============================================================================================================================================*/
// 🧩 Convert one line of Stamp text into a real font-outline cubic contour, no placeholder geometry.

#include "StampTextOutline.h"
#include <fstream>
#include <limits>
#include <vector>

// 📝 STBTT_STATIC keeps every symbol this translation unit declares internal, so this copy of
// stb_truetype never reaches the same link as the separate copy ImGui vendors and compiles with
// external linkage inside imgui_draw.cpp (see Engine/Render/StbImageImplementation.cpp for the
// matching single-TU convention this file follows for stb_image).
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include <stb_truetype.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace Frontier
{
namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     TYPEFACE LOADING
//------------------------------------------------------------------------------------------------------------------------

std::vector<unsigned char> ReadTypefaceBytes(std::string_view Path)
{
    std::ifstream Stream(std::string(Path), std::ios::binary | std::ios::ate);
    if (!Stream.is_open()) throw std::runtime_error("Could not open the Stamp text typeface.");
    const auto Size = Stream.tellg();
    if (Size <= 0 || Size > 32 * 1024 * 1024) throw std::runtime_error("Stamp text typeface size is out of bounds.");
    std::vector<unsigned char> Bytes(static_cast<size_t>(Size));
    Stream.seekg(0);
    Stream.read(reinterpret_cast<char *>(Bytes.data()), Size);
    if (!Stream) throw std::runtime_error("Could not read the Stamp text typeface.");
    return Bytes;
}

// 📝 Decodes one UTF-8 codepoint starting at Index, advancing Index past it. Malformed sequences
// fall back to a single replacement codepoint rather than throwing, since an author's pasted text
// should still produce whatever glyphs are decodable instead of refusing the whole component.
uint32_t DecodeUtf8Codepoint(std::string_view Text, size_t &Index)
{
    const unsigned char Lead = (unsigned char)Text[Index];
    size_t Continuations = 0;
    uint32_t Codepoint = 0;
    if ((Lead & 0x80) == 0) { Codepoint = Lead; Continuations = 0; }
    else if ((Lead & 0xE0) == 0xC0) { Codepoint = Lead & 0x1F; Continuations = 1; }
    else if ((Lead & 0xF0) == 0xE0) { Codepoint = Lead & 0x0F; Continuations = 2; }
    else if ((Lead & 0xF8) == 0xF0) { Codepoint = Lead & 0x07; Continuations = 3; }
    else { ++Index; return 0xFFFD; }
    ++Index;
    for (size_t Step = 0; Step < Continuations; ++Step)
    {
        if (Index >= Text.size() || ((unsigned char)Text[Index] & 0xC0) != 0x80) return 0xFFFD;
        Codepoint = (Codepoint << 6) | ((unsigned char)Text[Index] & 0x3F);
        ++Index;
    }
    return Codepoint;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   QUADRATIC TO CUBIC
//------------------------------------------------------------------------------------------------------------------------

using Coordinate = std::array<float, 2>;

// 📝 Exact, lossless quadratic-to-cubic elevation, since VectorCubicSegment carries no quadratic case.
VectorCubicSegment ElevateQuadratic(Coordinate Start, Coordinate Control, Coordinate End)
{
    const Coordinate C1{Start[0] + (Control[0] - Start[0]) * (2.f / 3.f), Start[1] + (Control[1] - Start[1]) * (2.f / 3.f)};
    const Coordinate C2{End[0] + (Control[0] - End[0]) * (2.f / 3.f), End[1] + (Control[1] - End[1]) * (2.f / 3.f)};
    return {{Start[0], Start[1], C1[0], C1[1]}, {C2[0], C2[1], End[0], End[1]}};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   GLYPH TRANSLATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Appends one glyph's already-positioned, already-scaled contours (TrueType's own winding kept
// intact) as a run of closed cubic spans, tracking the ink bounding box as it goes.
void AppendGlyphContours(const stbtt_vertex *Vertices, int VertexCount, float OffsetX, float OffsetY, float Scale,
    std::vector<VectorCubicSegment> &Segments, Coordinate &Minimum, Coordinate &Maximum)
{
    const auto ToWorld = [&](float RawX, float RawY) -> Coordinate {
        // 📝 TrueType is y-up around the baseline; the engine canvas is y-down, so flip Y on entry.
        // OffsetY shifts a whole wrapped line downward (it is 0 for every glyph on the first line).
        return {OffsetX + RawX * Scale, OffsetY - RawY * Scale};
    };
    const auto Track = [&](Coordinate Point)
    {
        Minimum[0] = std::min(Minimum[0], Point[0]); Minimum[1] = std::min(Minimum[1], Point[1]);
        Maximum[0] = std::max(Maximum[0], Point[0]); Maximum[1] = std::max(Maximum[1], Point[1]);
    };
    Coordinate Cursor{0, 0}, ContourStart{0, 0};
    bool Open = false;
    for (int Index = 0; Index < VertexCount; ++Index)
    {
        const stbtt_vertex &Vertex = Vertices[Index];
        if (Vertex.type == STBTT_vmove)
        {
            Cursor = ContourStart = ToWorld(float(Vertex.x), float(Vertex.y));
            Track(Cursor);
            Open = true;
            continue;
        }
        if (!Open) continue;
        const Coordinate Finish = ToWorld(float(Vertex.x), float(Vertex.y));
        if (Vertex.type == STBTT_vline)
        {
            Segments.push_back({{Cursor[0], Cursor[1], Cursor[0], Cursor[1]}, {Finish[0], Finish[1], Finish[0], Finish[1]}});
            Track(Finish);
        }
        else if (Vertex.type == STBTT_vcurve)
        {
            const Coordinate Control = ToWorld(float(Vertex.cx), float(Vertex.cy));
            Segments.push_back(ElevateQuadratic(Cursor, Control, Finish));
            Track(Control); Track(Finish);
        }
        else if (Vertex.type == STBTT_vcubic)
        {
            const Coordinate ControlA = ToWorld(float(Vertex.cx), float(Vertex.cy));
            const Coordinate ControlB = ToWorld(float(Vertex.cx1), float(Vertex.cy1));
            Segments.push_back({{Cursor[0], Cursor[1], ControlA[0], ControlA[1]}, {ControlB[0], ControlB[1], Finish[0], Finish[1]}});
            Track(ControlA); Track(ControlB); Track(Finish);
        }
        else continue;
        Cursor = Finish;
    }
    // 📝 stb_truetype contours are implicitly closed back to their move-to; this engine's nonzero
    // winding walk needs that closing span authored explicitly, just like CubicPathEditing does.
    if (Open && (Cursor[0] != ContourStart[0] || Cursor[1] != ContourStart[1]))
        Segments.push_back({{Cursor[0], Cursor[1], Cursor[0], Cursor[1]}, {ContourStart[0], ContourStart[1], ContourStart[0], ContourStart[1]}});
}

// 📝 Shapes one already-wrapped line of text (no embedded newlines) at a fixed vertical OffsetY,
// left-aligned at CursorX 0. Shared by the single-line path (one call, OffsetY 0) and the wrapped
// multi-line path (one call per packed line, OffsetY advancing by LineHeight each time) so both
// stay byte-for-byte identical in how a line of glyphs is shaped.
void LayoutLine(const stbtt_fontinfo &Face, std::string_view Line, float OffsetY, float Scale,
    std::vector<VectorCubicSegment> &Segments, Coordinate &Minimum, Coordinate &Maximum)
{
    float CursorX = 0;
    int PreviousGlyph = -1;
    for (size_t Index = 0; Index < Line.size();)
    {
        const uint32_t Codepoint = DecodeUtf8Codepoint(Line, Index);
        const int Glyph = stbtt_FindGlyphIndex(&Face, int(Codepoint));
        if (PreviousGlyph >= 0 && Glyph > 0)
            CursorX += float(stbtt_GetGlyphKernAdvance(&Face, PreviousGlyph, Glyph)) * Scale;
        if (Glyph > 0)
        {
            stbtt_vertex *Vertices = nullptr;
            const int VertexCount = stbtt_GetGlyphShape(&Face, Glyph, &Vertices);
            if (Vertices)
            {
                AppendGlyphContours(Vertices, VertexCount, CursorX, OffsetY, Scale, Segments, Minimum, Maximum);
                stbtt_FreeShape(&Face, Vertices);
            }
            int Advance = 0, Bearing = 0;
            stbtt_GetGlyphHMetrics(&Face, Glyph, &Advance, &Bearing);
            CursorX += float(Advance) * Scale;
        }
        else
        {
            // 📝 A codepoint with no glyph (an unmapped character, or the typeface's own notdef)
            // still reserves its width so later text in the run does not collapse onto it.
            int Advance = 0, Bearing = 0;
            stbtt_GetCodepointHMetrics(&Face, int(Codepoint), &Advance, &Bearing);
            CursorX += Advance > 0 ? float(Advance) * Scale : Scale * 300.f;
        }
        PreviousGlyph = Glyph > 0 ? Glyph : -1;
    }
}

// 📝 Measures one word's pen advance (identical metrics to LayoutLine, minus the shape extraction)
// so greedy word-wrap can pack words onto lines before any geometry is actually built.
float MeasureWordAdvance(const stbtt_fontinfo &Face, std::string_view Word, float Scale)
{
    float CursorX = 0;
    int PreviousGlyph = -1;
    for (size_t Index = 0; Index < Word.size();)
    {
        const uint32_t Codepoint = DecodeUtf8Codepoint(Word, Index);
        const int Glyph = stbtt_FindGlyphIndex(&Face, int(Codepoint));
        if (PreviousGlyph >= 0 && Glyph > 0)
            CursorX += float(stbtt_GetGlyphKernAdvance(&Face, PreviousGlyph, Glyph)) * Scale;
        int Advance = 0, Bearing = 0;
        if (Glyph > 0) { stbtt_GetGlyphHMetrics(&Face, Glyph, &Advance, &Bearing); CursorX += float(Advance) * Scale; }
        else
        {
            stbtt_GetCodepointHMetrics(&Face, int(Codepoint), &Advance, &Bearing);
            CursorX += Advance > 0 ? float(Advance) * Scale : Scale * 300.f;
        }
        PreviousGlyph = Glyph > 0 ? Glyph : -1;
    }
    return CursorX;
}

// 📝 Greedy word-wrap: packs space-separated words onto as few lines as fit within WrapWidth,
// always placing at least one word per line (an overlong single word still gets its own line
// rather than vanishing) so the caller always receives a non-empty layout.
std::vector<std::string> WrapText(const stbtt_fontinfo &Face, std::string_view Text, float Scale, float WrapWidth)
{
    const float SpaceAdvance = MeasureWordAdvance(Face, " ", Scale);
    std::vector<std::string> Lines;
    std::string Current;
    float CurrentWidth = 0;
    size_t Index = 0;
    while (Index < Text.size())
    {
        size_t WordStart = Index;
        while (WordStart < Text.size() && Text[WordStart] == ' ') ++WordStart;
        if (WordStart >= Text.size()) break;
        size_t WordEnd = WordStart;
        while (WordEnd < Text.size() && Text[WordEnd] != ' ') ++WordEnd;
        const std::string_view Word = Text.substr(WordStart, WordEnd - WordStart);
        const float WordWidth = MeasureWordAdvance(Face, Word, Scale);
        if (!Current.empty() && CurrentWidth + SpaceAdvance + WordWidth > WrapWidth)
        {
            Lines.push_back(Current);
            Current.clear();
            CurrentWidth = 0;
        }
        if (!Current.empty()) { Current += ' '; CurrentWidth += SpaceAdvance; }
        Current += std::string(Word);
        CurrentWidth += WordWidth;
        Index = WordEnd;
    }
    if (!Current.empty()) Lines.push_back(Current);
    if (Lines.empty()) Lines.push_back(std::string(Text));
    return Lines;
}
} // namespace

uint32_t AppendStampTextOutline(VectorAuthoringDocument &Document, std::string_view Text, float PointSize, std::array<float, 4> Colour, std::string_view TypefacePath, float WrapWidth)
{
    if (Text.empty() || Text.size() > StampTextCharacterLimit) throw std::runtime_error("Stamp text length is out of its authored bound.");
    if (!std::isfinite(PointSize) || PointSize <= 0 || PointSize > 2000) throw std::runtime_error("Stamp text point size is out of its authored bound.");
    for (const float Component : Colour) if (!std::isfinite(Component)) throw std::runtime_error("Stamp text colour must be finite.");

    const auto Bytes = ReadTypefaceBytes(TypefacePath);
    stbtt_fontinfo Face{};
    const int Offset = stbtt_GetFontOffsetForIndex(Bytes.data(), 0);
    if (Offset < 0 || !stbtt_InitFont(&Face, Bytes.data(), Offset)) throw std::runtime_error("The Stamp text typeface could not be read.");

    const float Scale = stbtt_ScaleForMappingEmToPixels(&Face, PointSize);
    std::vector<VectorCubicSegment> Segments;
    Coordinate Minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Coordinate Maximum{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
    // 📝 WrapWidth <= 0 keeps the historical single-line layout (one LayoutLine call covering the
    // whole string verbatim, spacing included). WrapWidth > 0 greedily word-wraps onto as many
    // lines as needed to stay within it, each line shaped at its own vertical offset, so a caption
    // that keeps growing fills the box downward with more lines instead of shrinking indefinitely
    // on a single line.
    if (WrapWidth > 0)
    {
        int Ascent = 0, Descent = 0, LineGap = 0;
        stbtt_GetFontVMetrics(&Face, &Ascent, &Descent, &LineGap);
        const float LineHeight = float(Ascent - Descent + LineGap) * Scale;
        const std::vector<std::string> Lines = WrapText(Face, Text, Scale, WrapWidth);
        for (size_t LineIndex = 0; LineIndex < Lines.size(); ++LineIndex)
            LayoutLine(Face, Lines[LineIndex], float(LineIndex) * LineHeight, Scale, Segments, Minimum, Maximum);
    }
    else
    {
        LayoutLine(Face, Text, 0.f, Scale, Segments, Minimum, Maximum);
    }
    if (Segments.empty() || Minimum[0] > Maximum[0] || Minimum[1] > Maximum[1])
        throw std::runtime_error("Stamp text resolves to no visible ink.");

    const float Width = std::max(.01f, Maximum[0] - Minimum[0]), Height = std::max(.01f, Maximum[1] - Minimum[1]);
    const float CentreX = (Minimum[0] + Maximum[0]) * .5f, CentreY = (Minimum[1] + Maximum[1]) * .5f;
    for (auto &Segment : Segments)
    {
        Segment.StartControl[0] = (Segment.StartControl[0] - CentreX) / Width; Segment.StartControl[1] = (Segment.StartControl[1] - CentreY) / Height;
        Segment.StartControl[2] = (Segment.StartControl[2] - CentreX) / Width; Segment.StartControl[3] = (Segment.StartControl[3] - CentreY) / Height;
        Segment.ControlEnd[0] = (Segment.ControlEnd[0] - CentreX) / Width; Segment.ControlEnd[1] = (Segment.ControlEnd[1] - CentreY) / Height;
        Segment.ControlEnd[2] = (Segment.ControlEnd[2] - CentreX) / Width; Segment.ControlEnd[3] = (Segment.ControlEnd[3] - CentreY) / Height;
    }

    const auto SegmentOffset = uint32_t(Document.Segments.size());
    Document.Segments.insert(Document.Segments.end(), Segments.begin(), Segments.end());

    VectorCoverageInstruction Instruction;
    Instruction.Topology = {6, SegmentOffset, uint32_t(Segments.size()), 0}; // 6 = CubicContour, nonzero fill rule
    Instruction.Bounds = {CentreX, CentreY, Width, Height};
    Instruction.Transform = {0, 0, 1, 0}; // No rotation, no stroke, full opacity, solid paint
    Instruction.Colour = Instruction.ColourEnd = Instruction.StrokeColour = Colour;
    Instruction.Paint = {0, .45f, 6, 1}; // Filled (Paint.w = 1), matching every other authored shape's defaults

    const auto InstructionIndex = uint32_t(Document.Instructions.size());
    Document.Instructions.push_back(Instruction);
    return InstructionIndex;
}
}

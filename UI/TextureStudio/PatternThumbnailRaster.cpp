/*============================================================================================================================================
                                                         PATTERNTHUMBNAILRASTER.CPP
============================================================================================================================================*/
// 🧩 Gallery thumbnails are real SVG documents rasterized by ThorVG: one vector encoding of the
// authoring document, anti-aliased at whatever extent the atlas asks for. No home-grown scanline
// fill remains; primitive half-extents match the coverage shader (Bounds.zw * .5).

#include "UI/TextureStudio/PatternThumbnailRaster.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <thorvg.h>

namespace Frontier
{
namespace
{
struct ReleasePaint
{
    void operator()(tvg::Picture *Picture) const { tvg::Paint::rel(Picture); }
};

void EnsureThorVg()
{
    static const bool Ready = tvg::Initializer::init(0) == tvg::Result::Success;
    if (!Ready) throw std::runtime_error("ThorVG runtime initialization failed.");
}

std::string Number(float Value)
{
    char Buffer[32]{};
    std::snprintf(Buffer, sizeof(Buffer), "%.3f", double(Value));
    return Buffer;
}

std::string ColourLiteral(const std::array<float, 4> &Colour)
{
    const auto Channel = [](float Value) { return int(std::lround(std::clamp(Value, 0.f, 1.f) * 255)); };
    char Buffer[32]{};
    std::snprintf(Buffer, sizeof(Buffer), "rgb(%d,%d,%d)", Channel(Colour[0]), Channel(Colour[1]), Channel(Colour[2]));
    return Buffer;
}

std::string PatternInstructionStyle(const VectorCoverageInstruction &Source, float Opacity)
{
    std::string Text;
    const std::string Fill = ColourLiteral(Source.Colour);
    if (Source.Paint[3] != 0)
        Text += " fill=\"" + Fill + "\" fill-opacity=\"" + Number(std::clamp(Source.Colour[3] * Opacity, 0.f, 1.f)) + "\"";
    else
        Text += " fill=\"none\"";
    if (Source.Transform[1] > 0)
    {
        const bool Explicit = (Source.Topology[3] & VectorCoverageStrokeColourExplicit) != 0;
        const auto &Line = Explicit ? Source.StrokeColour : Source.Colour;
        Text += " stroke=\"" + ColourLiteral(Line) + "\" stroke-opacity=\"" +
                Number(std::clamp(Line[3] * Opacity, 0.f, 1.f)) + "\" stroke-width=\"" + Number(Source.Transform[1]) +
                "\" stroke-linejoin=\"round\" stroke-linecap=\"round\"";
    }
    return Text;
}

// 📝 Shared recursive emitter: appends one instruction (and, for Boolean/composite roots, both
// children) to Svg in absolute tile-space coordinates. Used both for the whole-document gallery
// encoding and the single-layer row-thumbnail encoding below, so the two never drift apart.
void EmitPatternInstruction(const VectorAuthoringDocument &Document, uint32_t Root, uint32_t Depth, std::string &Svg)
{
    if (Root >= Document.Instructions.size() || Depth > 16) return;
    const auto &Source = Document.Instructions[Root];
    const uint32_t Category = Source.Topology[0];
    if (Category > 6)
    {
        // 📝 Boolean/composite roots emit both children in order, matching card paint order.
        EmitPatternInstruction(Document, Source.Topology[1], Depth + 1, Svg);
        if (Source.Topology[0] != 17 && Source.Topology[0] != 24 && Source.Topology[0] != 25)
            EmitPatternInstruction(Document, Source.Topology[2], Depth + 1, Svg);
        return;
    }
    const float Opacity = Source.Transform[2];
    if (!(Opacity > 0) || !std::isfinite(Opacity)) return;
    const float Rotation = Source.Transform[0];
    const float Sine = std::sin(Rotation), Cosine = std::cos(Rotation);
    const auto Place = [&](float LocalX, float LocalY)
    {
        return std::array<float, 2>{Source.Bounds[0] + Cosine * LocalX - Sine * LocalY,
            Source.Bounds[1] + Sine * LocalX + Cosine * LocalY};
    };
    const std::string Paint = PatternInstructionStyle(Source, Opacity);
    // 📝 Bounds.zw are full extents; the coverage shader halves them, and so does this encoding.
    const float HalfX = Source.Bounds[2] * .5f, HalfY = Source.Bounds[3] * .5f;
    const auto Points = [&](const std::vector<std::array<float, 2>> &Loop)
    {
        std::string Text = "<polygon points=\"";
        for (const auto &Point : Loop) Text += Number(Point[0]) + "," + Number(Point[1]) + " ";
        Text += "\"" + Paint + "/>";
        return Text;
    };
    if (Category == 0)
    {
        Svg += Points({Place(-HalfX, -HalfY), Place(HalfX, -HalfY), Place(HalfX, HalfY), Place(-HalfX, HalfY)});
        return;
    }
    if (Category == 1)
    {
        Svg += "<ellipse cx=\"" + Number(Source.Bounds[0]) + "\" cy=\"" + Number(Source.Bounds[1]) + "\" rx=\"" +
               Number(HalfX) + "\" ry=\"" + Number(HalfY) + "\" transform=\"rotate(" +
               Number(Rotation * 57.2957795131f) + " " + Number(Source.Bounds[0]) + " " +
               Number(Source.Bounds[1]) + ")\"" + Paint + "/>";
        return;
    }
    if (Category <= 5)
    {
        uint32_t Count = Category == 2 ? 4 : Category == 3 ? 3 : uint32_t(std::max(Source.Paint[2], 3.f));
        if (Category == 5) Count *= 2;
        std::vector<std::array<float, 2>> Loop;
        for (uint32_t Corner = 0; Corner < Count; ++Corner)
        {
            const float Angle = -1.57079632679f + 6.28318530718f * float(Corner) / float(Count);
            const float Radius = Category == 5 && Corner % 2 == 1 ? Source.Paint[1] : 1;
            Loop.push_back(Place(std::cos(Angle) * HalfX * Radius, std::sin(Angle) * HalfY * Radius));
        }
        Svg += Points(Loop);
        return;
    }
    const uint32_t Start = Source.Topology[1], Count = Source.Topology[2];
    if (Count == 0 || uint64_t(Start) + Count > Document.Segments.size()) return;
    // 📝 Segments live in unit-cage space scaled by the full Bounds, exactly as the shader scales them.
    const auto Cage = [&](float X, float Y) { return Place(X * Source.Bounds[2], Y * Source.Bounds[3]); };
    std::string Path = "<path d=\"";
    bool Pen = false;
    std::array<float, 2> Last{0, 0}, First{0, 0};
    for (uint32_t Offset = 0; Offset < Count; ++Offset)
    {
        const auto &Segment = Document.Segments[Start + Offset];
        const auto A = Cage(Segment.StartControl[0], Segment.StartControl[1]);
        const auto B = Cage(Segment.StartControl[2], Segment.StartControl[3]);
        const auto C = Cage(Segment.ControlEnd[0], Segment.ControlEnd[1]);
        const auto D = Cage(Segment.ControlEnd[2], Segment.ControlEnd[3]);
        if (!Pen || std::hypot(Last[0] - A[0], Last[1] - A[1]) > 1e-3f)
        {
            Path += "M" + Number(A[0]) + " " + Number(A[1]) + " ";
            if (!Pen) First = A;
            Pen = true;
        }
        Path += "C" + Number(B[0]) + " " + Number(B[1]) + " " + Number(C[0]) + " " + Number(C[1]) + " " +
                Number(D[0]) + " " + Number(D[1]) + " ";
        Last = D;
    }
    if (Pen && std::hypot(Last[0] - First[0], Last[1] - First[1]) <= 1.5f) Path += "Z";
    Path += "\"" + Paint + "/>";
    Svg += Path;
}
} // namespace

std::string EncodePatternThumbnailSvg(const VectorAuthoringDocument &Document, uint32_t BackgroundAbgr)
{
    std::string Svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\">";
    {
        char Buffer[96]{};
        std::snprintf(Buffer, sizeof(Buffer), "<rect width=\"512\" height=\"512\" fill=\"rgb(%u,%u,%u)\"/>",
            BackgroundAbgr & 255, (BackgroundAbgr >> 8) & 255, (BackgroundAbgr >> 16) & 255);
        Svg += Buffer;
    }
    for (const auto &Layer : Document.Layers)
    {
        if (!QueryVectorAuthoringVisibility(Document, Layer)) continue;
        EmitPatternInstruction(Document, Layer.Root, 0, Svg);
    }
    Svg += "</svg>";
    return Svg;
}

std::string EncodePatternLayerThumbnailSvg(const VectorAuthoringDocument &Document, uint32_t Token, uint32_t BackgroundAbgr)
{
    const auto Position = std::find_if(Document.Layers.begin(), Document.Layers.end(),
        [Token](const auto &Layer) { return Layer.Token == Token; });
    if (Position == Document.Layers.end()) throw std::runtime_error("Pattern layer thumbnail needs an authenticated layer token.");
    // 📝 Crop to the layer's own tight extent (the same bounds the selection hitbox hugs), never the
    // full 512 tile, so a small motif reads clearly instead of shrinking to a speck in a tile-sized frame.
    const std::array<uint32_t, 1> Selection{Token};
    const auto Extent = QueryVectorSelectionExtent(Document, Selection);
    const float Span = std::max({Extent.Bounds[2], Extent.Bounds[3], 1.f}) * 1.25f;
    const float Left = Extent.Bounds[0] - Span * .5f, Top = Extent.Bounds[1] - Span * .5f;
    char ViewBox[96]{};
    std::snprintf(ViewBox, sizeof(ViewBox), "%.3f %.3f %.3f %.3f", double(Left), double(Top), double(Span), double(Span));
    std::string Svg = std::string("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"") + ViewBox + "\">";
    {
        char Buffer[192]{};
        std::snprintf(Buffer, sizeof(Buffer), "<rect x=\"%.3f\" y=\"%.3f\" width=\"%.3f\" height=\"%.3f\" fill=\"rgb(%u,%u,%u)\"/>",
            double(Left), double(Top), double(Span), double(Span),
            BackgroundAbgr & 255, (BackgroundAbgr >> 8) & 255, (BackgroundAbgr >> 16) & 255);
        Svg += Buffer;
    }
    if (QueryVectorAuthoringVisibility(Document, *Position)) EmitPatternInstruction(Document, Position->Root, 0, Svg);
    Svg += "</svg>";
    return Svg;
}

std::vector<uint8_t> RasterizePatternThumbnailSvg(
    const VectorAuthoringDocument &Document, uint32_t Extent, uint32_t BackgroundAbgr)
{
    if (!Extent || Extent > 1024) throw std::runtime_error("Thumbnail raster extent is unbounded.");
    const std::string Svg = EncodePatternThumbnailSvg(Document, BackgroundAbgr);
    EnsureThorVg();
    std::vector<uint8_t> Pixels(size_t(Extent) * Extent * 4, 0);
    std::unique_ptr<tvg::SwCanvas> Canvas(tvg::SwCanvas::gen());
    std::unique_ptr<tvg::Picture, ReleasePaint> Picture(tvg::Picture::gen());
    if (!Canvas || !Picture) throw std::runtime_error("ThorVG thumbnail canvas allocation failed.");
    bool Ok = Picture->load(Svg.data(), uint32_t(Svg.size()), "svg", nullptr, true) == tvg::Result::Success;
    if (Ok) Ok = Picture->size(float(Extent), float(Extent)) == tvg::Result::Success;
    if (Ok) Ok = Canvas->target(reinterpret_cast<uint32_t *>(Pixels.data()), Extent, Extent, Extent,
                     tvg::ColorSpace::ABGR8888S) == tvg::Result::Success;
    if (Ok)
    {
        Ok = Canvas->add(Picture.get()) == tvg::Result::Success;
        if (Ok) Picture.release();
    }
    if (Ok) Ok = Canvas->draw(true) == tvg::Result::Success;
    if (Ok) Ok = Canvas->sync() == tvg::Result::Success;
    if (!Ok) throw std::runtime_error("ThorVG could not rasterize the pattern thumbnail SVG.");
    return Pixels;
}

std::vector<uint8_t> RasterizePatternLayerThumbnail(
    const VectorAuthoringDocument &Document, uint32_t Token, uint32_t Extent, uint32_t BackgroundAbgr)
{
    if (!Extent || Extent > 1024) throw std::runtime_error("Layer thumbnail raster extent is unbounded.");
    const std::string Svg = EncodePatternLayerThumbnailSvg(Document, Token, BackgroundAbgr);
    EnsureThorVg();
    std::vector<uint8_t> Pixels(size_t(Extent) * Extent * 4, 0);
    std::unique_ptr<tvg::SwCanvas> Canvas(tvg::SwCanvas::gen());
    std::unique_ptr<tvg::Picture, ReleasePaint> Picture(tvg::Picture::gen());
    if (!Canvas || !Picture) throw std::runtime_error("ThorVG layer-thumbnail canvas allocation failed.");
    bool Ok = Picture->load(Svg.data(), uint32_t(Svg.size()), "svg", nullptr, true) == tvg::Result::Success;
    if (Ok) Ok = Picture->size(float(Extent), float(Extent)) == tvg::Result::Success;
    if (Ok) Ok = Canvas->target(reinterpret_cast<uint32_t *>(Pixels.data()), Extent, Extent, Extent,
                     tvg::ColorSpace::ABGR8888S) == tvg::Result::Success;
    if (Ok)
    {
        Ok = Canvas->add(Picture.get()) == tvg::Result::Success;
        if (Ok) Picture.release();
    }
    if (Ok) Ok = Canvas->draw(true) == tvg::Result::Success;
    if (Ok) Ok = Canvas->sync() == tvg::Result::Success;
    if (!Ok) throw std::runtime_error("ThorVG could not rasterize the pattern layer thumbnail SVG.");
    return Pixels;
}

std::vector<uint8_t> BlitPatternThumbnailAtlas(
    const std::vector<std::vector<uint8_t>> &Thumbs, uint32_t TileExtent, uint32_t Columns, uint32_t BackgroundAbgr)
{
    if (!TileExtent || TileExtent > 512 || !Columns || Columns > 16 || Thumbs.size() > size_t(Columns) * Columns)
        throw std::runtime_error("Atlas blit exceeds bounded grid capacity.");
    for (const auto &Thumb : Thumbs)
        if (Thumb.size() != size_t(TileExtent) * TileExtent * 4)
            throw std::runtime_error("Atlas thumb does not match the tile extent.");
    const uint32_t Stride = TileExtent + 2, AtlasExtent = Stride * Columns;
    std::vector<uint8_t> Atlas(size_t(AtlasExtent) * AtlasExtent * 4);
    for (size_t Pixel = 0; Pixel < Atlas.size(); Pixel += 4)
    {
        Atlas[Pixel] = uint8_t(BackgroundAbgr & 255);
        Atlas[Pixel + 1] = uint8_t((BackgroundAbgr >> 8) & 255);
        Atlas[Pixel + 2] = uint8_t((BackgroundAbgr >> 16) & 255);
        Atlas[Pixel + 3] = 255;
    }
    for (size_t Slot = 0; Slot < Thumbs.size(); ++Slot)
    {
        const auto &Thumb = Thumbs[Slot];
        const uint32_t Left = uint32_t(Slot % Columns) * Stride + 1, Top = uint32_t(Slot / Columns) * Stride + 1;
        for (uint32_t Row = 0; Row < TileExtent; ++Row)
        {
            const uint8_t *Source = &Thumb[size_t(Row) * TileExtent * 4];
            uint8_t *Target = &Atlas[(size_t(Top + Row) * AtlasExtent + Left) * 4];
            std::copy_n(Source, size_t(TileExtent) * 4, Target);
            // 📝 Replicated gutters keep bilinear sampling inside the tile, mirroring the GPU atlas copies.
            std::copy_n(Source, 4, Target - 4);
            std::copy_n(Source + (size_t(TileExtent) - 1) * 4, 4, Target + size_t(TileExtent) * 4);
        }
        const uint8_t *FirstRow = &Atlas[(size_t(Top) * AtlasExtent + Left - 1) * 4];
        uint8_t *TopGutter = &Atlas[(size_t(Top - 1) * AtlasExtent + Left - 1) * 4];
        std::copy_n(FirstRow, size_t(TileExtent + 2) * 4, TopGutter);
        const uint8_t *LastRow = &Atlas[(size_t(Top + TileExtent - 1) * AtlasExtent + Left - 1) * 4];
        uint8_t *BottomGutter = &Atlas[(size_t(Top + TileExtent) * AtlasExtent + Left - 1) * 4];
        std::copy_n(LastRow, size_t(TileExtent + 2) * 4, BottomGutter);
    }
    return Atlas;
}
} // namespace Frontier

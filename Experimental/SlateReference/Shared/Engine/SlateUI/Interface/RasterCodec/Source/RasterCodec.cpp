#define _CRT_SECURE_NO_WARNINGS
//============================================================================================================================================
//                                                            RASTERCODEC.CPP
//============================================================================================================================================
// 🧩 Scanline coverage, bilinear texture reads, straight-alpha source-over — the whole pipeline is arithmetic on the byte extent.

#include "SlateUI/Interface/RasterCodec/Api/RasterCodec.h"

#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <system_error>

namespace Slate
{
Deliver<bool> RasterCodec::ApplyAtlas(void* Identity)
{
    ImGuiIO& VendorIO = ImGui::GetIO();
    if (VendorIO.Fonts == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no font atlas stands constructed" });

    unsigned char* Configuration = nullptr;
    int X = 0, Y = 0;
    VendorIO.Fonts->GetTexDataAsRGBA32(&Configuration, &X, &Y);
    if (Configuration == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the atlas resolved to no ordinates" });

    AtlasIdentity         = Identity;
    AtlasXExtent  = static_cast<std::uint32_t>(X);
    AtlasYExtent = static_cast<std::uint32_t>(Y);
    AtlasData.assign(Configuration, Configuration + static_cast<std::size_t>(X) * Y * 4u);
    VendorIO.Fonts->SetTexID(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(Identity)));
    return Deliver<bool>::Result(true);
}

namespace
{

/// 🧩 One resolved texture the rasterizer samples.
/// tag   internal
struct ResolvedTexture
{
    const std::uint8_t* Configuration    = nullptr;   // [-] - borrowed RGBA
    std::uint32_t       XExtent  = 1u;        // [px]
    std::uint32_t       YExtent = 1u;        // [px]
    bool                Current     = false;     // [-] - a real texture, else the white constant
};

/// 🧩 One bilinear texture read, clamped to the extent.
/// in    X   [-]  normalised sample coordinate along, nought to one
/// in    Y  [-]  normalised sample coordinate across, nought to one
/// note  ⚠️ The vendor records normalised sample ordinates. Reading them as pixel ordinates lands every
///       glyph on the atlas's leading white block, which draws text as solid rectangles.
/// cost  🚩
void SampleTexture(const ResolvedTexture& Texture, float X, float Y, float (&Coordinate)[4])
{
    if (!Texture.Current)
    {
        Coordinate[0] = Coordinate[1] = Coordinate[2] = Coordinate[3] = 255.0f;
        return;
    }

    // ① Normalised to pixel-centre ordinates, then clamped so the corner reads stay inside the extent.
    const float MappedX  = X  * static_cast<float>(Texture.XExtent)  - 0.5f;
    const float MappedY = Y * static_cast<float>(Texture.YExtent) - 0.5f;

    const float ClampedX = MappedX  < 0.0f ? 0.0f : (MappedX  > Texture.XExtent  - 1.0f ? Texture.XExtent  - 1.0f : MappedX);
    const float ClampedY = MappedY < 0.0f ? 0.0f : (MappedY > Texture.YExtent - 1.0f ? Texture.YExtent - 1.0f : MappedY);

    const std::uint32_t X0 = static_cast<std::uint32_t>(ClampedX);
    const std::uint32_t Y0 = static_cast<std::uint32_t>(ClampedY);
    const std::uint32_t X1 = X0 + 1u < Texture.XExtent ? X0 + 1u : X0;
    const std::uint32_t Y1 = Y0 + 1u < Texture.YExtent ? Y0 + 1u : Y0;
    const float FractionX = X - static_cast<float>(X0);
    const float FractionY = Y - static_cast<float>(Y0);

    for (std::uint32_t Component = 0u; Component < 4u; ++Component)
    {
        const float A = static_cast<float>(Texture.Configuration[(static_cast<std::size_t>(Y0) * Texture.XExtent + X0) * 4u + Component]);
        const float B = static_cast<float>(Texture.Configuration[(static_cast<std::size_t>(Y0) * Texture.XExtent + X1) * 4u + Component]);
        const float C = static_cast<float>(Texture.Configuration[(static_cast<std::size_t>(Y1) * Texture.XExtent + X0) * 4u + Component]);
        const float D = static_cast<float>(Texture.Configuration[(static_cast<std::size_t>(Y1) * Texture.XExtent + X1) * 4u + Component]);
        Coordinate[Component] = (A * (1.0f - FractionX) + B * FractionX) * (1.0f - FractionY)
                            + (C * (1.0f - FractionX) + D * FractionX) * FractionY;
    }
}

}   // namespace

void RasterCodec::Rasterize(const void* RecordedDrawData, PixelSpace& Extent)
{
    const ImDrawData* Recorded = static_cast<const ImDrawData*>(RecordedDrawData);

    // ①① The atlas may have baked late in the tick — resolve its ordinates fresh, every translation.
    if (AtlasIdentity != nullptr)
    {
        unsigned char* Configuration = nullptr;
        int X = 0, Y = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&Configuration, &X, &Y);
        if (Configuration != nullptr)
        {
            AtlasXExtent  = static_cast<std::uint32_t>(X);
            AtlasYExtent = static_cast<std::uint32_t>(Y);
            AtlasData.assign(Configuration, Configuration + static_cast<std::size_t>(X) * Y * 4u);
        }
    }
    Extent.Configuration.assign(static_cast<std::size_t>(Extent.YExtent) * Extent.XExtent * 4u, 0u);

    const auto ResolveTexture = [&](ImTextureID Identity) -> ResolvedTexture
    {
        if (reinterpret_cast<void*>(static_cast<std::uintptr_t>(Identity)) == AtlasIdentity && AtlasIdentity != nullptr)
            return { AtlasData.data(), AtlasXExtent, AtlasYExtent, true };
        for (const PictureDeclaration& Picture : Applied)
            if (Picture.Identity == reinterpret_cast<void*>(static_cast<std::uintptr_t>(Identity)))
                return { Picture.Configuration, Picture.XExtent, Picture.YExtent, true };
        return { nullptr, 1u, 1u, false };
    };

    for (int ListIndex = 0; ListIndex < Recorded->CmdListsCount; ++ListIndex)
    {
        const ImDrawList* List = Recorded->CmdLists[ListIndex];
        const ImDrawVert* VertexRun = List->VtxBuffer.Data;
        const ImDrawIdx*  IndexRun  = List->IdxBuffer.Data;

        for (int CommandIndex = 0; CommandIndex < List->CmdBuffer.Size; ++CommandIndex)
        {
            const ImDrawCmd& Command = List->CmdBuffer[CommandIndex];
            if (Command.UserCallback != nullptr)
                continue;

            const ResolvedTexture Texture = ResolveTexture(Command.GetTexID());
            const float ClipLeft  = Command.ClipRect.x < 0.0f ? 0.0f : Command.ClipRect.x;
            const float ClipTop = Command.ClipRect.y < 0.0f ? 0.0f : Command.ClipRect.y;
            const float ClipRight   = Command.ClipRect.z > static_cast<float>(Extent.XExtent)  ? static_cast<float>(Extent.XExtent)  : Command.ClipRect.z;
            const float ClipBottom  = Command.ClipRect.w > static_cast<float>(Extent.YExtent) ? static_cast<float>(Extent.YExtent) : Command.ClipRect.w;

            for (unsigned int Triangle = 0u; Triangle < Command.ElemCount; Triangle += 3u)
            {
                const ImDrawVert& VertexA = VertexRun[IndexRun[Command.IdxOffset + Triangle + 0u]];
                const ImDrawVert& VertexB = VertexRun[IndexRun[Command.IdxOffset + Triangle + 1u]];
                const ImDrawVert& VertexC = VertexRun[IndexRun[Command.IdxOffset + Triangle + 2u]];

                // ① Signed edge products — the coverage test, orientation-corrected.
                const float Area = (VertexB.pos.x - VertexA.pos.x) * (VertexC.pos.y - VertexA.pos.y)
                                 - (VertexC.pos.x - VertexA.pos.x) * (VertexB.pos.y - VertexA.pos.y);
                if (std::fabs(Area) < 1.0e-9f)
                    continue;
                const float Orientation = Area > 0.0f ? 1.0f : -1.0f;

                float MinimumX  = VertexA.pos.x, MaximumX  = VertexA.pos.x;
                float MinimumY = VertexA.pos.y, MaximumY = VertexA.pos.y;
                const ImDrawVert* Corners[3] = { &VertexA, &VertexB, &VertexC };
                for (const ImDrawVert* Corner : Corners)
                {
                    MinimumX  = MinimumX  < Corner->pos.x ? MinimumX  : Corner->pos.x;
                    MaximumX   = MaximumX   > Corner->pos.x ? MaximumX   : Corner->pos.x;
                    MinimumY = MinimumY < Corner->pos.y ? MinimumY : Corner->pos.y;
                    MaximumY  = MaximumY  > Corner->pos.y ? MaximumY  : Corner->pos.y;
                }
                MinimumX  = MinimumX  > ClipLeft  ? MinimumX  : ClipLeft;
                MaximumX   = MaximumX   < ClipRight   ? MaximumX   : ClipRight;
                MinimumY = MinimumY > ClipTop ? MinimumY : ClipTop;
                MaximumY  = MaximumY  < ClipBottom  ? MaximumY  : ClipBottom;

                const std::int32_t YBegin = static_cast<std::int32_t>(MinimumY);
                const std::int32_t YEnd   = static_cast<std::int32_t>(MaximumY + 1.0f);
                const std::int32_t XBegin  = static_cast<std::int32_t>(MinimumX);
                const std::int32_t XEnd    = static_cast<std::int32_t>(MaximumX + 1.0f);

                for (std::int32_t Y = YBegin; Y < YEnd && Y < static_cast<std::int32_t>(Extent.YExtent); ++Y)
                {
                    if (Y < 0)
                        continue;
                    for (std::int32_t X = XBegin; X < XEnd && X < static_cast<std::int32_t>(Extent.XExtent); ++X)
                    {
                        if (X < 0)
                            continue;

                        const float CentreX  = static_cast<float>(X) + 0.5f;
                        const float CentreY = static_cast<float>(Y) + 0.5f;

                        // ⚠️ An edge product carries the share of the corner it stands opposite: edge BC weighs
                        //    corner A. Reading edge AB as A's share rotates every interpolated colour and sample
                        //    coordinate one corner round, which scrambles glyph coverage and haloes the fringe.
                        const float XBC = ((VertexC.pos.x - VertexB.pos.x) * (CentreY - VertexB.pos.y)
                                             - (CentreX - VertexB.pos.x) * (VertexC.pos.y - VertexB.pos.y)) * Orientation;
                        const float XCA = ((VertexA.pos.x - VertexC.pos.x) * (CentreY - VertexC.pos.y)
                                             - (CentreX - VertexC.pos.x) * (VertexA.pos.y - VertexC.pos.y)) * Orientation;
                        const float XAB = ((VertexB.pos.x - VertexA.pos.x) * (CentreY - VertexA.pos.y)
                                             - (CentreX - VertexA.pos.x) * (VertexB.pos.y - VertexA.pos.y)) * Orientation;
                        if (XBC < 0.0f || XCA < 0.0f || XAB < 0.0f)
                            continue;

                        const float InverseArea = 1.0f / Area * Orientation;
                        const float ShareA = XBC * InverseArea;
                        const float ShareB = XCA * InverseArea;
                        const float ShareC = XAB * InverseArea;

                        float Colour[4];
                        for (std::uint32_t Component = 0u; Component < 4u; ++Component)
                        {
                            const float ChannelA = static_cast<float>((VertexA.col >> (Component * 8u)) & 0xFFu);
                            const float ChannelB = static_cast<float>((VertexB.col >> (Component * 8u)) & 0xFFu);
                            const float ChannelC = static_cast<float>((VertexC.col >> (Component * 8u)) & 0xFFu);
                            Colour[Component] = ChannelA * ShareA + ChannelB * ShareB + ChannelC * ShareC;
                        }

                        const float SampleX = VertexA.uv.x * ShareA + VertexB.uv.x * ShareB + VertexC.uv.x * ShareC;
                        const float SampleY = VertexA.uv.y * ShareA + VertexB.uv.y * ShareB + VertexC.uv.y * ShareC;
                        float TextureCoordinate[4];
                        SampleTexture(Texture, SampleX, SampleY, TextureCoordinate);

                        // ② Modulate, then source-over in straight alpha onto the extent.
                        const float SourceAlpha = (Colour[3] / 255.0f) * (TextureCoordinate[3] / 255.0f);
                        if (SourceAlpha <= 0.0f)
                            continue;

                        std::uint8_t* Pixel = &Extent.Configuration[(static_cast<std::size_t>(Y) * Extent.XExtent + X) * 4u];
                        const float CurrentAlpha = Pixel[3] / 255.0f;
                        const float BlendedAlpha = SourceAlpha + CurrentAlpha * (1.0f - SourceAlpha);

                        for (std::uint32_t Component = 0u; Component < 3u; ++Component)
                        {
                            const float SourceChannel = (Colour[Component] / 255.0f) * (TextureCoordinate[Component] / 255.0f) * 255.0f;
                            const float CurrentChannel = Pixel[Component];
                            const float Blended = SourceAlpha * SourceChannel + CurrentAlpha * (1.0f - SourceAlpha) * CurrentChannel;
                            Pixel[Component] = static_cast<std::uint8_t>(BlendedAlpha > 0.0f ? Blended / BlendedAlpha + 0.5f : 0u);
                        }
                        Pixel[3] = static_cast<std::uint8_t>(BlendedAlpha * 255.0f + 0.5f);
                    }
                }
            }
        }
    }
}

Deliver<bool> RasterCodec::WriteRawDump(const PixelSpace& Extent, const char* Path)
{
    std::FILE* Stream = std::fopen(Path, "wb");
    if (Stream == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the raw dump path failed to open" });

    std::fwrite("RIFTRAW1", 1u, 8u, Stream);
    const std::uint32_t Header[2] = { Extent.XExtent, Extent.YExtent };
    std::fwrite(Header, sizeof(std::uint32_t), 2u, Stream);
    std::fwrite(Extent.Configuration.data(), 1u, Extent.Configuration.size(), Stream);
    std::fclose(Stream);
    return Deliver<bool>::Result(true);
}

namespace
{

/// 🧩 The CRC-32 of one run, polynomial 0xEDB88320, table-driven.
/// cost  🚩
std::uint32_t CyclicRedundancyCheck(const std::uint8_t* Run, std::size_t Extent)
{
    static std::uint32_t Current[256];
    static bool Applied = false;
    if (!Applied)
    {
        for (std::uint32_t Index = 0u; Index < 256u; ++Index)
        {
            std::uint32_t Remainder = Index;
            for (int Cycle = 0; Cycle < 8; ++Cycle)
                Remainder = (Remainder & 1u) ? (Remainder >> 1) ^ 0xEDB88320u : (Remainder >> 1);
            Current[Index] = Remainder;
        }
        Applied = true;
    }
    std::uint32_t Check = 0xFFFFFFFFu;
    for (std::size_t Index = 0u; Index < Extent; ++Index)
        Check = Current[(Check ^ Run[Index]) & 0xFFu] ^ (Check >> 8);
    return Check ^ 0xFFFFFFFFu;
}

/// 🧩 The Adler-32 of one run.
/// cost  🚩
std::uint32_t AdlerThirtyTwo(const std::uint8_t* Run, std::size_t Extent)
{
    std::uint32_t Lower = 1u;
    std::uint32_t Upper = 0u;
    for (std::size_t Index = 0u; Index < Extent; ++Index)
    {
        Lower = (Lower + Run[Index]) % 65521u;
        Upper = (Upper + Lower) % 65521u;
    }
    return (Upper << 16) | Lower;
}

/// 🧩 Writes one big-endian coordinate.
/// cost  ✔️
void WriteBigEndian(std::FILE* Stream, std::uint32_t Coordinate)
{
    const std::uint8_t Run[4] = { static_cast<std::uint8_t>(Coordinate >> 24), static_cast<std::uint8_t>(Coordinate >> 16),
                                  static_cast<std::uint8_t>(Coordinate >> 8), static_cast<std::uint8_t>(Coordinate) };
    std::fwrite(Run, 1u, 4u, Stream);
}

}   // namespace

Deliver<bool> RasterCodec::WritePortableNetworkGraphic(const PixelSpace& Extent, const char* Path)
{
    // ① The directories along the path are created when absent.
    std::filesystem::path Declared(Path);
    if (Declared.has_parent_path() && !Declared.parent_path().empty())
    {
        std::error_code Deliver;
        std::filesystem::create_directories(Declared.parent_path(), Deliver);   // 📝 failures surface at fopen below
    }

    std::FILE* Stream = std::fopen(Path, "wb");
    if (Stream == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the proof path failed to open" });

    // ② Scanlines — filter 0, one stride each.
    const std::size_t Stride = static_cast<std::size_t>(Extent.XExtent) * 4u;
    std::vector<std::uint8_t> Scanlines((static_cast<std::size_t>(Extent.YExtent)) * (Stride + 1u), 0u);
    for (std::uint32_t Y = 0u; Y < Extent.YExtent && Extent.YExtent > 0u; ++Y)
        if (Stride > 0u)
            std::memcpy(&Scanlines[static_cast<std::size_t>(Y) * (Stride + 1u) + 1u],
                        &Extent.Configuration[static_cast<std::size_t>(Y) * Stride], Stride);

    // ③ zlib stream of stored blocks — honest, uncompressed, dependency-free.
    const std::size_t PayloadExtent = Scanlines.size();
    std::vector<std::uint8_t> Streamed;
    Streamed.reserve(PayloadExtent + PayloadExtent / 65535u * 5u + 16u);
    Streamed.push_back(0x78u);   // [-] - CMF: deflate, 32K window
    Streamed.push_back(0x01u);   // [-] - FLG: no dictionary, fastest
    std::size_t Cursor = 0u;
    while (Cursor < PayloadExtent)
    {
        const std::size_t Block = PayloadExtent - Cursor > 65535u ? 65535u : PayloadExtent - Cursor;
        const bool Terminal = Cursor + Block >= PayloadExtent;
        Streamed.push_back(Terminal ? 1u : 0u);
        Streamed.push_back(static_cast<std::uint8_t>(Block & 0xFFu));
        Streamed.push_back(static_cast<std::uint8_t>(Block >> 8));
        Streamed.push_back(static_cast<std::uint8_t>(~Block & 0xFFu));
        Streamed.push_back(static_cast<std::uint8_t>(~Block >> 8));
        Streamed.insert(Streamed.end(), Scanlines.begin() + static_cast<std::ptrdiff_t>(Cursor),
                        Scanlines.begin() + static_cast<std::ptrdiff_t>(Cursor + Block));
        Cursor += Block;
    }
    const std::uint32_t Adler = AdlerThirtyTwo(Scanlines.data(), Scanlines.size());
    Streamed.push_back(static_cast<std::uint8_t>(Adler >> 24));  Streamed.push_back(static_cast<std::uint8_t>(Adler >> 16));
    Streamed.push_back(static_cast<std::uint8_t>(Adler >> 8));   Streamed.push_back(static_cast<std::uint8_t>(Adler));

    // ④ The chunks — signature, header, payload, end.
    const auto PresentChunk = [&](const std::uint8_t Tag[4], const std::vector<std::uint8_t>& Body)
    {
        WriteBigEndian(Stream, static_cast<std::uint32_t>(Body.size()));
        std::fwrite(Tag, 1u, 4u, Stream);
        std::fwrite(Body.data(), 1u, Body.size(), Stream);
        std::uint32_t Check = CyclicRedundancyCheck(Tag, 4u);
        // ①① CRC runs over tag then body; the table call per byte keeps it simple and honest.
        std::vector<std::uint8_t> Joined(Tag, Tag + 4u);
        Joined.insert(Joined.end(), Body.begin(), Body.end());
        Check = CyclicRedundancyCheck(Joined.data(), Joined.size());
        WriteBigEndian(Stream, Check);
    };

    const std::uint8_t Signature[8] = { 0x89u, 'P', 'N', 'G', '\r', '\n', 0x1Au, '\n' };
    std::fwrite(Signature, 1u, 8u, Stream);

    std::vector<std::uint8_t> Header;
    Header.insert(Header.end(), { static_cast<std::uint8_t>(Extent.XExtent >> 24), static_cast<std::uint8_t>(Extent.XExtent >> 16),
                                  static_cast<std::uint8_t>(Extent.XExtent >> 8), static_cast<std::uint8_t>(Extent.XExtent),
                                  static_cast<std::uint8_t>(Extent.YExtent >> 24), static_cast<std::uint8_t>(Extent.YExtent >> 16),
                                  static_cast<std::uint8_t>(Extent.YExtent >> 8), static_cast<std::uint8_t>(Extent.YExtent),
                                  8u, 6u, 0u, 0u, 0u });
    PresentChunk(reinterpret_cast<const std::uint8_t*>("IHDR"), Header);
    PresentChunk(reinterpret_cast<const std::uint8_t*>("IDAT"), Streamed);
    PresentChunk(reinterpret_cast<const std::uint8_t*>("IEND"), {});

    std::fclose(Stream);
    return Deliver<bool>::Result(true);
}

}   // namespace Slate

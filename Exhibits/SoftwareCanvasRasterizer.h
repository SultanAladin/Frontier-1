/*============================================================================================================================================
                                                  SOFTWARECANVASRASTERIZER.H
============================================================================================================================================*/
// 🧩 Header-only software ImDrawData triangle rasterizer and lossless PNG 1.2 encoder for headless CTest validation

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Docking/WorkspaceTab.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace Frontier::Exhibits
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class SoftwareRasterCanvas
{
public:
    SoftwareRasterCanvas(uint32_t CanvasWidthPx, uint32_t CanvasHeightPx, Components::RgbaColor8 ClearColor)
        : WidthPx(CanvasWidthPx)
        , HeightPx(CanvasHeightPx)
        , PixelBuffer(static_cast<std::size_t>(CanvasWidthPx) * static_cast<std::size_t>(CanvasHeightPx) * 4u, 0u)
    {
        Clear(ClearColor);
    }

    void Clear(Components::RgbaColor8 ClearColor) noexcept
    {
        for (std::size_t Offset = 0u; Offset + 3u < PixelBuffer.size(); Offset += 4u)
        {
            PixelBuffer[Offset + 0u] = ClearColor.Red;
            PixelBuffer[Offset + 1u] = ClearColor.Green;
            PixelBuffer[Offset + 2u] = ClearColor.Blue;
            PixelBuffer[Offset + 3u] = ClearColor.Alpha;
        }
    }

    [[nodiscard]] uint32_t Width() const noexcept
    {
        return WidthPx;
    }

    [[nodiscard]] uint32_t Height() const noexcept
    {
        return HeightPx;
    }

    [[nodiscard]] Components::RgbaColor8 SamplePixel(uint32_t X, uint32_t Y) const noexcept
    {
        if (X >= WidthPx || Y >= HeightPx)
        {
            return {};
        }
        const std::size_t Offset = (static_cast<std::size_t>(Y) * WidthPx + X) * 4u;
        return Components::RgbaColor8{
            PixelBuffer[Offset + 0u],
            PixelBuffer[Offset + 1u],
            PixelBuffer[Offset + 2u],
            PixelBuffer[Offset + 3u],
        };
    }

    void RasterizeImGuiBatches(
        const ImDrawData* RenderedBatches,
        int               DestinationOriginX = 0,
        int               DestinationOriginY = 0) noexcept
    {
        if (RenderedBatches == nullptr || ImGui::GetCurrentContext() == nullptr)
        {
            return;
        }

        ImFontAtlas* FontSheet = ImGui::GetIO().Fonts;

        const float ShiftX = static_cast<float>(DestinationOriginX) - RenderedBatches->DisplayPos.x;
        const float ShiftY = static_cast<float>(DestinationOriginY) - RenderedBatches->DisplayPos.y;

        for (int BatchIndex = 0; BatchIndex < RenderedBatches->CmdListsCount; ++BatchIndex)
        {
            const ImDrawList* CommandBuffer = RenderedBatches->CmdLists[BatchIndex];
            const ImDrawVert* VertexArray   = CommandBuffer->VtxBuffer.Data;
            const ImDrawIdx*  IndexArray    = CommandBuffer->IdxBuffer.Data;

            for (int CmdIndex = 0; CmdIndex < CommandBuffer->CmdBuffer.Size; ++CmdIndex)
            {
                const ImDrawCmd& DrawCommand = CommandBuffer->CmdBuffer[CmdIndex];
                if (DrawCommand.UserCallback != nullptr)
                {
                    DrawCommand.UserCallback(CommandBuffer, &DrawCommand);
                    continue;
                }

                const ImTextureData* TextureSource = DrawCommand.TexRef._TexData
                                                         ? DrawCommand.TexRef._TexData
                                                         : (FontSheet ? FontSheet->TexData : nullptr);
                const unsigned char* TexturePixels = TextureSource ? static_cast<const unsigned char*>(TextureSource->Pixels) : nullptr;
                const int            TextureWidth  = TextureSource ? TextureSource->Width : 0;
                const int            TextureHeight = TextureSource ? TextureSource->Height : 0;
                const bool           Rgba32Texture = TextureSource && TextureSource->Format == ImTextureFormat_RGBA32;

                const int ClipMinX = std::max(0, static_cast<int>(std::floor(DrawCommand.ClipRect.x + ShiftX)));
                const int ClipMinY = std::max(0, static_cast<int>(std::floor(DrawCommand.ClipRect.y + ShiftY)));
                const int ClipMaxX = std::min(static_cast<int>(WidthPx), static_cast<int>(std::ceil(DrawCommand.ClipRect.z + ShiftX)));
                const int ClipMaxY = std::min(static_cast<int>(HeightPx), static_cast<int>(std::ceil(DrawCommand.ClipRect.w + ShiftY)));

                if (ClipMinX >= ClipMaxX || ClipMinY >= ClipMaxY)
                {
                    continue;
                }

                const unsigned int IndexOffset  = DrawCommand.IdxOffset;
                const unsigned int VertexOffset = DrawCommand.VtxOffset;

                for (unsigned int TriOffset = 0u; TriOffset + 2u < DrawCommand.ElemCount; TriOffset += 3u)
                {
                    ImDrawVert V0 = VertexArray[VertexOffset + IndexArray[IndexOffset + TriOffset + 0u]];
                    ImDrawVert V1 = VertexArray[VertexOffset + IndexArray[IndexOffset + TriOffset + 1u]];
                    ImDrawVert V2 = VertexArray[VertexOffset + IndexArray[IndexOffset + TriOffset + 2u]];

                    V0.pos.x += ShiftX;
                    V0.pos.y += ShiftY;
                    V1.pos.x += ShiftX;
                    V1.pos.y += ShiftY;
                    V2.pos.x += ShiftX;
                    V2.pos.y += ShiftY;

                    RasterizeTriangle(
                        V0,
                        V1,
                        V2,
                        ClipMinX,
                        ClipMinY,
                        ClipMaxX,
                        ClipMaxY,
                        TexturePixels,
                        TextureWidth,
                        TextureHeight,
                        Rgba32Texture);
                }
            }
        }
    }

    [[nodiscard]] bool EncodePngToFile(const std::filesystem::path& OutputPath) const
    {
        return WritePngFile(OutputPath);
    }

    [[nodiscard]] bool WritePngFile(const std::filesystem::path& OutputPath) const
    {
        if (OutputPath.has_parent_path())
        {
            std::error_code IoFailure{};
            std::filesystem::create_directories(OutputPath.parent_path(), IoFailure);
        }

        std::vector<uint8_t> RawScanlines{};
        RawScanlines.reserve(static_cast<std::size_t>(HeightPx) * (1u + static_cast<std::size_t>(WidthPx) * 4u));
        for (uint32_t Row = 0u; Row < HeightPx; ++Row)
        {
            RawScanlines.push_back(0u);
            const std::size_t RowStart = static_cast<std::size_t>(Row) * WidthPx * 4u;
            RawScanlines.insert(
                RawScanlines.end(),
                PixelBuffer.begin() + static_cast<std::ptrdiff_t>(RowStart),
                PixelBuffer.begin() + static_cast<std::ptrdiff_t>(RowStart + WidthPx * 4u));
        }

        std::vector<uint8_t> ZlibStream{};
        ZlibStream.push_back(0x78u);
        ZlibStream.push_back(0x01u);

        std::size_t Offset = 0u;
        while (Offset < RawScanlines.size())
        {
            const std::size_t Remaining = RawScanlines.size() - Offset;
            const uint16_t    BlockLen  = static_cast<uint16_t>(std::min<std::size_t>(Remaining, 65535u));
            const bool        LastBlock = (Offset + BlockLen == RawScanlines.size());
            const uint16_t    Negated   = static_cast<uint16_t>(~BlockLen);

            ZlibStream.push_back(LastBlock ? 0x01u : 0x00u);
            ZlibStream.push_back(static_cast<uint8_t>(BlockLen & 0xFFu));
            ZlibStream.push_back(static_cast<uint8_t>((BlockLen >> 8u) & 0xFFu));
            ZlibStream.push_back(static_cast<uint8_t>(Negated & 0xFFu));
            ZlibStream.push_back(static_cast<uint8_t>((Negated >> 8u) & 0xFFu));
            ZlibStream.insert(
                ZlibStream.end(),
                RawScanlines.begin() + static_cast<std::ptrdiff_t>(Offset),
                RawScanlines.begin() + static_cast<std::ptrdiff_t>(Offset + BlockLen));
            Offset += BlockLen;
        }

        AppendBigEndian32(ZlibStream, ComputeAdler32(RawScanlines));

        std::vector<uint8_t> PngBytes = { 137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u };
        std::vector<uint8_t> IhdrBody{};
        AppendBigEndian32(IhdrBody, WidthPx);
        AppendBigEndian32(IhdrBody, HeightPx);
        IhdrBody.push_back(8u);
        IhdrBody.push_back(6u);
        IhdrBody.push_back(0u);
        IhdrBody.push_back(0u);
        IhdrBody.push_back(0u);

        AppendPngChunk(PngBytes, { 'I', 'H', 'D', 'R' }, IhdrBody);
        AppendPngChunk(PngBytes, { 'I', 'D', 'A', 'T' }, ZlibStream);
        AppendPngChunk(PngBytes, { 'I', 'E', 'N', 'D' }, {});

        std::ofstream Stream(OutputPath, std::ios::binary | std::ios::trunc);
        if (!Stream.is_open())
        {
            return false;
        }
        Stream.write(reinterpret_cast<const char*>(PngBytes.data()), static_cast<std::streamsize>(PngBytes.size()));
        return Stream.good();
    }

private:
    uint32_t             WidthPx     = 0u; // [px] - Raster canvas pixel width
    uint32_t             HeightPx    = 0u; // [px] - Raster canvas pixel height
    std::vector<uint8_t> PixelBuffer {};   // [-]  - Packed RGBA8 scanline buffer

    [[nodiscard]] static double Edge(double Ax, double Ay, double Bx, double By, double Px, double Py) noexcept
    {
        return (Px - Ax) * (By - Ay) - (Py - Ay) * (Bx - Ax);
    }

    [[nodiscard]] static bool TopLeft(double Ax, double Ay, double Bx, double By) noexcept
    {
        const double Dy = By - Ay;
        const double Dx = Bx - Ax;
        return Dy < 0.0 || (Dy == 0.0 && Dx > 0.0);
    }

    void RasterizeTriangle(
        ImDrawVert           A,
        ImDrawVert           B,
        ImDrawVert           C,
        int                  ClipMinX,
        int                  ClipMinY,
        int                  ClipMaxX,
        int                  ClipMaxY,
        const unsigned char* TexturePixels,
        int                  TextureWidth,
        int                  TextureHeight,
        bool                 Rgba32Texture) noexcept
    {
        double Area = Edge(A.pos.x, A.pos.y, B.pos.x, B.pos.y, C.pos.x, C.pos.y);
        if (std::abs(Area) < 1e-6)
        {
            return;
        }
        if (Area < 0.0)
        {
            std::swap(B, C);
            Area = -Area;
        }
        const double InvArea = 1.0 / Area;

        const int MinX = std::max(ClipMinX, static_cast<int>(std::floor(std::min({ A.pos.x, B.pos.x, C.pos.x }))));
        const int MaxX = std::min(ClipMaxX - 1, static_cast<int>(std::ceil(std::max({ A.pos.x, B.pos.x, C.pos.x }))));
        const int MinY = std::max(ClipMinY, static_cast<int>(std::floor(std::min({ A.pos.y, B.pos.y, C.pos.y }))));
        const int MaxY = std::min(ClipMaxY - 1, static_cast<int>(std::ceil(std::max({ A.pos.y, B.pos.y, C.pos.y }))));

        for (int PixelY = MinY; PixelY <= MaxY; ++PixelY)
        {
            const double SampleY = static_cast<double>(PixelY) + 0.5;
            for (int PixelX = MinX; PixelX <= MaxX; ++PixelX)
            {
                const double SampleX = static_cast<double>(PixelX) + 0.5;
                const double W0      = Edge(B.pos.x, B.pos.y, C.pos.x, C.pos.y, SampleX, SampleY);
                const double W1      = Edge(C.pos.x, C.pos.y, A.pos.x, A.pos.y, SampleX, SampleY);
                const double W2      = Edge(A.pos.x, A.pos.y, B.pos.x, B.pos.y, SampleX, SampleY);

                if (W0 < 0.0 || (W0 == 0.0 && !TopLeft(B.pos.x, B.pos.y, C.pos.x, C.pos.y)))
                {
                    continue;
                }
                if (W1 < 0.0 || (W1 == 0.0 && !TopLeft(C.pos.x, C.pos.y, A.pos.x, A.pos.y)))
                {
                    continue;
                }
                if (W2 < 0.0 || (W2 == 0.0 && !TopLeft(A.pos.x, A.pos.y, B.pos.x, B.pos.y)))
                {
                    continue;
                }

                const float Bary0 = static_cast<float>(W0 * InvArea);
                const float Bary1 = static_cast<float>(W1 * InvArea);
                const float Bary2 = static_cast<float>(W2 * InvArea);

                const ImVec4 Color0 = ImGui::ColorConvertU32ToFloat4(A.col);
                const ImVec4 Color1 = ImGui::ColorConvertU32ToFloat4(B.col);
                const ImVec4 Color2 = ImGui::ColorConvertU32ToFloat4(C.col);

                float SrcRed   = Bary0 * Color0.x + Bary1 * Color1.x + Bary2 * Color2.x;
                float SrcGreen = Bary0 * Color0.y + Bary1 * Color1.y + Bary2 * Color2.y;
                float SrcBlue  = Bary0 * Color0.z + Bary1 * Color1.z + Bary2 * Color2.z;
                float SrcAlpha = Bary0 * Color0.w + Bary1 * Color1.w + Bary2 * Color2.w;

                if (TexturePixels != nullptr && TextureWidth > 0 && TextureHeight > 0)
                {
                    const float U      = std::clamp(Bary0 * A.uv.x + Bary1 * B.uv.x + Bary2 * C.uv.x, 0.0f, 1.0f);
                    const float V      = std::clamp(Bary0 * A.uv.y + Bary1 * B.uv.y + Bary2 * C.uv.y, 0.0f, 1.0f);
                    const auto  Sample = SampleBilinearSheet(TexturePixels, TextureWidth, TextureHeight, Rgba32Texture, U, V);
                    SrcRed   *= Sample[0];
                    SrcGreen *= Sample[1];
                    SrcBlue  *= Sample[2];
                    SrcAlpha *= Sample[3];
                }

                if (SrcAlpha <= (1.0f / 512.0f))
                {
                    continue;
                }

                const std::size_t Offset   = (static_cast<std::size_t>(PixelY) * WidthPx + static_cast<std::size_t>(PixelX)) * 4u;
                const float       DstRed   = static_cast<float>(PixelBuffer[Offset + 0u]) / 255.0f;
                const float       DstGreen = static_cast<float>(PixelBuffer[Offset + 1u]) / 255.0f;
                const float       DstBlue  = static_cast<float>(PixelBuffer[Offset + 2u]) / 255.0f;
                const float       InvAlpha = 1.0f - SrcAlpha;

                PixelBuffer[Offset + 0u] = static_cast<uint8_t>(std::clamp((SrcRed * SrcAlpha + DstRed * InvAlpha) * 255.0f + 0.5f, 0.0f, 255.0f));
                PixelBuffer[Offset + 1u] = static_cast<uint8_t>(std::clamp((SrcGreen * SrcAlpha + DstGreen * InvAlpha) * 255.0f + 0.5f, 0.0f, 255.0f));
                PixelBuffer[Offset + 2u] = static_cast<uint8_t>(std::clamp((SrcBlue * SrcAlpha + DstBlue * InvAlpha) * 255.0f + 0.5f, 0.0f, 255.0f));
                PixelBuffer[Offset + 3u] = 255u;
            }
        }
    }

    [[nodiscard]] static std::array<float, 4> SampleBilinearSheet(
        const unsigned char* Pixels,
        int                  Width,
        int                  Height,
        bool                 Rgba32,
        float                U,
        float                V) noexcept
    {
        const float TexX = U * static_cast<float>(Width) - 0.5f;
        const float TexY = V * static_cast<float>(Height) - 0.5f;
        const int   X0   = std::clamp(static_cast<int>(std::floor(TexX)), 0, Width - 1);
        const int   Y0   = std::clamp(static_cast<int>(std::floor(TexY)), 0, Height - 1);
        const int   X1   = std::clamp(X0 + 1, 0, Width - 1);
        const int   Y1   = std::clamp(Y0 + 1, 0, Height - 1);
        const float Fx   = std::clamp(TexX - std::floor(TexX), 0.0f, 1.0f);
        const float Fy   = std::clamp(TexY - std::floor(TexY), 0.0f, 1.0f);

        auto FetchTexel = [&](int X, int Y) noexcept -> std::array<float, 4>
        {
            constexpr float Inv = 1.0f / 255.0f;
            if (Rgba32)
            {
                const std::size_t Idx = (static_cast<std::size_t>(Y) * Width + X) * 4u;
                return { Pixels[Idx + 0u] * Inv, Pixels[Idx + 1u] * Inv, Pixels[Idx + 2u] * Inv, Pixels[Idx + 3u] * Inv };
            }
            const std::size_t Idx = static_cast<std::size_t>(Y) * Width + X;
            return { 1.0f, 1.0f, 1.0f, Pixels[Idx] * Inv };
        };

        const auto T00 = FetchTexel(X0, Y0);
        const auto T10 = FetchTexel(X1, Y0);
        const auto T01 = FetchTexel(X0, Y1);
        const auto T11 = FetchTexel(X1, Y1);

        std::array<float, 4> Result{};
        for (std::size_t Channel = 0u; Channel < 4u; ++Channel)
        {
            const float Top    = T00[Channel] * (1.0f - Fx) + T10[Channel] * Fx;
            const float Bottom = T01[Channel] * (1.0f - Fx) + T11[Channel] * Fx;
            Result[Channel]    = Top * (1.0f - Fy) + Bottom * Fy;
        }
        return Result;
    }

    static void AppendBigEndian32(std::vector<uint8_t>& Destination, uint32_t Word32)
    {
        Destination.push_back(static_cast<uint8_t>((Word32 >> 24u) & 0xFFu));
        Destination.push_back(static_cast<uint8_t>((Word32 >> 16u) & 0xFFu));
        Destination.push_back(static_cast<uint8_t>((Word32 >> 8u) & 0xFFu));
        Destination.push_back(static_cast<uint8_t>(Word32 & 0xFFu));
    }

    [[nodiscard]] static uint32_t ComputeAdler32(const std::vector<uint8_t>& Bytes) noexcept
    {
        constexpr uint32_t Modulo = 65521u;
        uint32_t           SumA   = 1u;
        uint32_t           SumB   = 0u;
        for (uint8_t Byte : Bytes)
        {
            SumA = (SumA + Byte) % Modulo;
            SumB = (SumB + SumA) % Modulo;
        }
        return (SumB << 16u) | SumA;
    }

    [[nodiscard]] static uint32_t ComputeCrc32(const uint8_t* Bytes, std::size_t Length, uint32_t Seed = 0xFFFFFFFFu) noexcept
    {
        uint32_t Crc = Seed;
        for (std::size_t Index = 0u; Index < Length; ++Index)
        {
            Crc ^= Bytes[Index];
            for (int Bit = 0; Bit < 8; ++Bit)
            {
                const uint32_t Mask = static_cast<uint32_t>(-static_cast<int32_t>(Crc & 1u));
                Crc                 = (Crc >> 1u) ^ (0xEDB88320u & Mask);
            }
        }
        return Crc;
    }

    static void AppendPngChunk(
        std::vector<uint8_t>&       Destination,
        std::array<char, 4>         Tag,
        const std::vector<uint8_t>& BodyBytes)
    {
        AppendBigEndian32(Destination, static_cast<uint32_t>(BodyBytes.size()));
        const std::size_t TagOffset = Destination.size();
        for (char Ch : Tag)
        {
            Destination.push_back(static_cast<uint8_t>(Ch));
        }
        Destination.insert(Destination.end(), BodyBytes.begin(), BodyBytes.end());
        const uint32_t Crc = ~ComputeCrc32(Destination.data() + TagOffset, 4u + BodyBytes.size(), 0xFFFFFFFFu);
        AppendBigEndian32(Destination, Crc);
    }
};

[[nodiscard]] inline std::filesystem::path ResolveRepositoryRootPath()
{
#ifdef FRONTIER_REPO_ROOT_DIR
    return std::filesystem::path(FRONTIER_REPO_ROOT_DIR);
#elif defined(FRONTIER_REPOSITORY_ROOT)
    return std::filesystem::path(FRONTIER_REPOSITORY_ROOT);
#else
    return std::filesystem::current_path();
#endif
}

[[nodiscard]] inline ImFont* LoadProjectZeroDmSansFont(ImGuiIO& Io, float BrowserCssPixels = 12.5f)
{
    ImFont*         Result   = nullptr;
    const auto      FontFile = ResolveRepositoryRootPath() / "EngineContent/FontsArchives/DM Sans/DMSans-Medium.ttf";
    std::error_code IoFailure{};
    if (std::filesystem::exists(FontFile, IoFailure))
    {
        ImFontConfig Config{};
        Config.OversampleH = 3;
        Config.OversampleV = 2;
        if (ImFont* Loaded = Io.Fonts->AddFontFromFileTTF(
                FontFile.string().c_str(),
                Components::ComputeImGuiFontPixelSize(BrowserCssPixels),
                &Config))
        {
            Io.FontDefault = Loaded;
            Result         = Loaded;
        }
    }
    if (Result == nullptr)
    {
        Result = Io.Fonts->AddFontDefault();
    }
    unsigned char* TexPixels = nullptr;
    int            TexW      = 0;
    int            TexH      = 0;
    Io.Fonts->GetTexDataAsRGBA32(&TexPixels, &TexW, &TexH);
    return Result;
}

} // namespace Frontier::Exhibits

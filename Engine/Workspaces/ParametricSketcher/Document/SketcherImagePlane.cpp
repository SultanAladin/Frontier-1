/*====================================================================================================================================
                                                        SOLIDARCIMAGEPLANE.CPP
====================================================================================================================================*/
// 🧩 Image planes: picture decoding (stb_image), path resolution, world placement checks

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherImagePlane.h"
#include "Engine/Codecs/ImageCodec.h"

#define STBI_WINDOWS_UTF8                                                                 // UTF-8 file names on Windows
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC                                                                     // 📝 Internal linkage: upstream Engine/Render links its own stb_image
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_HDR
#define STBI_NO_TGA
#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace Frontier::Workspaces::Sketcher
{

using namespace Frontier::GeometrySpace::Parametric;

namespace
{

[[nodiscard]] bool HasImageSpaceExtension(const std::filesystem::path& FilePath)
{
    std::string Ext = FilePath.extension().string();
    for (char& Ch : Ext)
    {
        Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
    }
    return Ext == ".imagespace";
}

[[nodiscard]] float SrgbByteToLinear(uint8_t Byte) noexcept
{
    const float C = static_cast<float>(Byte) / 255.0f;
    return (C <= 0.04045f) ? (C / 12.92f) : std::pow((C + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] uint8_t LinearToSrgbByte(float Linear) noexcept
{
    const float Clamped = std::clamp(Linear, 0.0f, 1.0f);
    const float Srgb    = (Clamped <= 0.0031308f)
                            ? (Clamped * 12.92f)
                            : (1.055f * std::pow(Clamped, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(Srgb * 255.0f), 0l, 255l));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SketcherPicture DecodeSketcherPicture(const std::string& Path)
{
    SketcherPicture Picture;
    Picture.Path = Path;
    const std::filesystem::path FilePath(Path);
    if (HasImageSpaceExtension(FilePath))
    {
        Frontier::Codecs::ImageSpaceDocumentModel Model{};
        std::string Diagnostic{};
        if (!Frontier::Codecs::ImageCodec::Decoder::DecodeFromFile(FilePath, Model, Diagnostic))
        {
            Picture.Refusal = "cannot read .ImageSpace " + Path + (Diagnostic.empty() ? std::string() : " (" + Diagnostic + ")");
            return Picture;
        }
        if (Model.NominalWidthPx == 0u || Model.NominalHeightPx == 0u)
        {
            Picture.Refusal = ".ImageSpace has zero dimensions: " + Path;
            return Picture;
        }
        const size_t ExpectedFloats = size_t(Model.NominalWidthPx) * size_t(Model.NominalHeightPx) * 4u;
        if (Model.LinearRgbaPreview.size() < ExpectedFloats)
        {
            Picture.Refusal = ".ImageSpace has no LinearRgbaCanvas pixel payload matching its dimensions: " + Path;
            return Picture;
        }
        Picture.Width  = Model.NominalWidthPx;
        Picture.Height = Model.NominalHeightPx;
        Picture.Pixels.resize(ExpectedFloats);
        for (size_t Pixel = 0u; Pixel < size_t(Picture.Width) * size_t(Picture.Height); ++Pixel)
        {
            const size_t Base = Pixel * 4u;
            Picture.Pixels[Base + 0u] = LinearToSrgbByte(Model.LinearRgbaPreview[Base + 0u]);
            Picture.Pixels[Base + 1u] = LinearToSrgbByte(Model.LinearRgbaPreview[Base + 1u]);
            Picture.Pixels[Base + 2u] = LinearToSrgbByte(Model.LinearRgbaPreview[Base + 2u]);
            Picture.Pixels[Base + 3u] = static_cast<uint8_t>(std::clamp(std::lround(Model.LinearRgbaPreview[Base + 3u] * 255.0f), 0l, 255l));
        }
        return Picture;
    }

    int Width = 0, Height = 0, Channels = 0;
    stbi_uc* Pixels = stbi_load(Path.c_str(), &Width, &Height, &Channels, 4);
    if (!Pixels)
    {
        const char* Reason = stbi_failure_reason();
        Picture.Refusal = "cannot read picture " + Path + (Reason ? std::string(" (") + Reason + ")" : std::string());
        return Picture;
    }
    Picture.Width  = static_cast<uint32_t>(Width);
    Picture.Height = static_cast<uint32_t>(Height);
    Picture.Pixels.assign(Pixels, Pixels + size_t(Width) * size_t(Height) * 4);
    stbi_image_free(Pixels);
    return Picture;
}

bool ExportSketcherPictureAsImageSpace(
    const SketcherPicture&       Picture,
    const std::filesystem::path& TargetPath,
    std::string&                 OutDiagnostic,
    std::string_view             DocumentTitle)
{
    if (!Picture.Refusal.empty() || Picture.Width == 0u || Picture.Height == 0u ||
        Picture.Pixels.size() < size_t(Picture.Width) * size_t(Picture.Height) * 4u)
    {
        OutDiagnostic = Picture.Refusal.empty() ? "Cannot export empty picture to .ImageSpace." : Picture.Refusal;
        return false;
    }

    Frontier::Codecs::ImageSpaceDocumentModel Model{};
    Model.DocumentTitle   = DocumentTitle.empty()
                            ? (TargetPath.stem().empty() ? std::string("ReferenceImage") : TargetPath.stem().string())
                            : std::string(DocumentTitle);
    Model.NominalWidthPx  = Picture.Width;
    Model.NominalHeightPx = Picture.Height;

    Frontier::Codecs::ImageCanvasLayerDescriptor Layer{};
    Layer.LayerTitle        = "ReferencePlane";
    Layer.OpacityFactor     = 1.0;
    Layer.VisibilityEnabled = true;
    Model.Layers.push_back(std::move(Layer));

    const size_t PixelCount = size_t(Picture.Width) * size_t(Picture.Height);
    Model.LinearRgbaPreview.resize(PixelCount * 4u);
    for (size_t Pixel = 0u; Pixel < PixelCount; ++Pixel)
    {
        const size_t Base = Pixel * 4u;
        Model.LinearRgbaPreview[Base + 0u] = SrgbByteToLinear(Picture.Pixels[Base + 0u]);
        Model.LinearRgbaPreview[Base + 1u] = SrgbByteToLinear(Picture.Pixels[Base + 1u]);
        Model.LinearRgbaPreview[Base + 2u] = SrgbByteToLinear(Picture.Pixels[Base + 2u]);
        Model.LinearRgbaPreview[Base + 3u] = static_cast<float>(Picture.Pixels[Base + 3u]) / 255.0f;
    }

    return Frontier::Codecs::ImageCodec::Encoder::EncodeToFile(TargetPath, Model, OutDiagnostic);
}

bool ConvertPictureFileToImageSpace(
    const std::filesystem::path& SourcePath,
    const std::filesystem::path& TargetPath,
    std::string&                 OutDiagnostic)
{
    const SketcherPicture Picture = DecodeSketcherPicture(SourcePath.string());
    if (!Picture.Refusal.empty())
    {
        OutDiagnostic = Picture.Refusal;
        return false;
    }
    return ExportSketcherPictureAsImageSpace(Picture, TargetPath, OutDiagnostic, SourcePath.stem().string());
}

std::string ResolveSketcherImagePath(const std::string& Path, const std::string& NamingFile)
{
    namespace Files = std::filesystem;
    const Files::path Given(Path);
    if (Given.is_absolute() || NamingFile.empty()) return Path;
    std::error_code Failure;
    if (Files::exists(Given, Failure)) return Path;
    const Files::path Beside = Files::path(NamingFile).parent_path() / Given;
    return Files::exists(Beside, Failure) ? Beside.lexically_normal().string() : Path;
}

Vector3 LocateSketcherImagePoint(const SketcherImagePlane& Plane, double Across, double Downward) noexcept
{
    return Plane.Corner + Plane.Right * Across + Plane.Down * Downward;
}

std::string ExamineSketcherImagePlane(const SketcherImagePlane& Plane)
{
    const std::string Where = "image plane " + (Plane.Name.empty() ? std::string("(unnamed)") : Plane.Name);
    if (Plane.Name.empty()) return Where + " has no Name";
    if (Plane.Path.empty()) return Where + " has no Path";
    for (const Vector3& Value : { Plane.Corner, Plane.Right, Plane.Down })
        if (!std::isfinite(Value.X) || !std::isfinite(Value.Y) || !std::isfinite(Value.Z)) return Where + ": Corner / Right / Down must be finite";
    const double Area = Length(Cross(Plane.Right, Plane.Down));
    if (!(Area > 1.0e-12)) return Where + ": Right and Down span no area";
    if (Plane.Crop[2] < Plane.Crop[0] || Plane.Crop[3] < Plane.Crop[1] || Plane.Crop[0] < 0 || Plane.Crop[1] < 0) return Where + ": Crop must be [left, top, right, bottom]";
    if (!(Plane.Opacity >= 0.0 && Plane.Opacity <= 1.0)) return Where + ": Opacity must lie in 0 … 1";
    return {};
}

} // namespace Frontier::Workspaces::Sketcher

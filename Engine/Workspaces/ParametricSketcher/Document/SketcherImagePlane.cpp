/*====================================================================================================================================
                                                        SOLIDARCIMAGEPLANE.CPP
====================================================================================================================================*/
// 🧩 Image planes: picture decoding (stb_image), path resolution, world placement checks

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherImagePlane.h"

#define STBI_WINDOWS_UTF8                                                                 // UTF-8 file names on Windows
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC                                                                     // 📝 Internal linkage: upstream Engine/Render links its own stb_image
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_HDR
#define STBI_NO_TGA
#include <stb_image.h>

#include <cmath>
#include <cstring>
#include <filesystem>

namespace Frontier::Workspaces::Sketcher
{

using namespace Frontier::GeometrySpace::Parametric;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SketcherPicture DecodeSketcherPicture(const std::string& Path)
{
    SketcherPicture Picture;
    Picture.Path = Path;
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

/*==========================================================================================================================================
                                                      MATERIALLIBRARYRENDERING.CPP
==========================================================================================================================================*/
// 🧩 Bake one requested dirty tile per submission using shared Slang programs, GPU scratch rasterization and one sampled atlas.

#include "MaterialLibraryRendering.h"
#include "MaterialPreviewGeometry.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Interface/ThumbnailAtlasCache.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <numbers>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <stb_image.h>

namespace Frontier
{
namespace
{
uint64_t ComputeMaterialFingerprint(const CompiledSurfaceMaterial &Material)
{
    uint64_t Digest = 14695981039346656037ull;
    const auto Accumulate = [&](const void *Bytes, size_t Count)
    {
        const auto *Octets = static_cast<const unsigned char *>(Bytes);
        for (size_t Index = 0; Index < Count; ++Index) { Digest ^= Octets[Index]; Digest *= 1099511628211ull; }
    };
    Accumulate(&Material.Scattering, sizeof(Material.Scattering));
    const auto AccumulatePyramid = [&](const MaterialTexturePyramid &Texture)
    {
        Accumulate(&Texture.Category, sizeof(Texture.Category));
        Accumulate(&Texture.Layers, sizeof(Texture.Layers));
        for (const auto &Level : Texture.Levels)
        {
            Accumulate(&Level.Width, sizeof(Level.Width));
            Accumulate(&Level.Height, sizeof(Level.Height));
        }
        Accumulate(Texture.Rgba.data(), Texture.Rgba.size());
        Accumulate(Texture.FloatRgba.data(), Texture.FloatRgba.size() * sizeof(float));
    };
    for (const auto &Texture : Material.Textures) AccumulatePyramid(Texture);
    // 📝 Native Pattern maps can change while the preview path and PBR coefficients remain identical.
    for (const auto &Texture : Material.AuthoringMaps) AccumulatePyramid(Texture);
    return Digest;
}
// 📝 Cheap change test for baked tiles: digest the authored source bytes plus size/write-time of every
// referenced Textures/ map. Library tiles are a baked atlas — re-evaluation must never decode or rebake
// a material whose inputs are unchanged. Returns 0 when the inputs cannot be authenticated (rebake).
uint64_t ComputeMaterialSourceDigest(const std::filesystem::path &Directory, const std::filesystem::path &RelativePath)
{
    std::error_code Error;
    std::ifstream Input(Directory / RelativePath, std::ios::binary | std::ios::ate);
    const auto Bytes = Input.tellg();
    if (!Input || Bytes <= 0 || Bytes > 1048576) return 0;
    std::string Source(size_t(Bytes), '\0');
    Input.seekg(0);
    if (!Input.read(Source.data(), Bytes)) return 0;
    uint64_t Digest = 14695981039346656037ull;
    const auto Accumulate = [&](const void *Data, size_t Count)
    {
        const auto *Octets = static_cast<const unsigned char *>(Data);
        for (size_t Index = 0; Index < Count; ++Index) { Digest ^= Octets[Index]; Digest *= 1099511628211ull; }
    };
    Accumulate(Source.data(), Source.size());
    size_t Search = 0;
    uint32_t Referenced = 0;
    while ((Search = Source.find("Textures/", Search)) != std::string::npos)
    {
        const auto End = Source.find('"', Search);
        if (End == std::string::npos) return 0;
        const auto TexturePath = (Directory / RelativePath.parent_path() /
            Source.substr(Search, End - Search)).lexically_normal();
        const auto Size = std::filesystem::file_size(TexturePath, Error);
        if (Error) return 0;
        const auto Written = std::filesystem::last_write_time(TexturePath, Error).time_since_epoch().count();
        if (Error) return 0;
        Accumulate(&Size, sizeof(Size));
        Accumulate(&Written, sizeof(Written));
        Search = End + 1;
        if (++Referenced > 64) return 0;
    }
    return Digest ? Digest : 1;
}
std::string DescribeMaterialMaps(const CompiledSurfaceMaterial &Material)
{
    std::ostringstream Line;
    const size_t Strata = Material.Scattering.Illumination.Configuration[0];
    Line << "strata=" << Strata << " masks=[";
    for (size_t Index = 0; Index < Strata && Index < 8; ++Index)
    {
        if (Index) Line << ',';
        Line << Material.Scattering.TextureUse[Index][0];
    }
    Line << "] slices=[";
    for (size_t Index = 0; Index < Strata && Index < 8; ++Index)
    {
        if (Index) Line << ';';
        Line << Material.Scattering.TextureUse[Index][1] << '/' << Material.Scattering.TextureUse[Index][2]
             << '/' << Material.Scattering.TextureUse[Index][3];
    }
    Line << "] maps=[";
    for (size_t Slot = 0; Slot < 3; ++Slot)
    {
        const auto &Pyramid = Material.Textures[Slot];
        if (Slot) Line << ',';
        if (Pyramid.Levels.empty()) { Line << "none"; continue; }
        const auto &Level = Pyramid.Levels[0];
        Line << Level.Width << 'x' << Level.Height;
        bool Varied = false;
        if (Level.Width > 1 || Level.Height > 1)
        {
            const size_t Count = size_t(Level.Width) * Level.Height;
            const size_t Step = Count / 2048 + 1;
            uint8_t Min[3] = {255, 255, 255}, Max[3] = {0, 0, 0};
            for (size_t Pixel = 0; Pixel < Count; Pixel += Step)
            {
                const size_t At = Level.Offset + Pixel * 4;
                if (At + 4 > Pyramid.Rgba.size()) break;
                for (size_t Channel = 0; Channel < 3; ++Channel)
                {
                    Min[Channel] = std::min(Min[Channel], Pyramid.Rgba[At + Channel]);
                    Max[Channel] = std::max(Max[Channel], Pyramid.Rgba[At + Channel]);
                }
            }
            for (size_t Channel = 0; Channel < 3; ++Channel)
                if (int(Max[Channel]) - int(Min[Channel]) > 8) Varied = true;
        }
        Line << (Varied ? '*' : '-');
    }
    Line << ']';
    return Line.str();
}
std::vector<uint32_t> DecodePreviewShader(const std::filesystem::path &Path)
{
    std::ifstream Input(Path, std::ios::binary | std::ios::ate);
    const auto Bytes = Input.tellg();
    if (!Input || Bytes < 20 || Bytes > 16777216 || Bytes % 4) throw std::runtime_error("Missing or malformed material shader archive.");
    std::vector<uint32_t> Words(size_t(Bytes) / 4);
    Input.seekg(0);
    if (!Input.read(reinterpret_cast<char *>(Words.data()), Bytes) || Words[0] != 0x07230203)
        throw std::runtime_error("Invalid material shader archive.");
    return Words;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ENVIRONMENT ARCHIVES
//------------------------------------------------------------------------------------------------------------------------

// 📝 One scanned equirect studio archive: scene-linear pixels at the shared sampling extent plus
// the lighting facts derived from them (mean radiance for strength normalization, brightest-region
// centroid for the key light). Naming follows Documentation/Plans/Ongoing/TextureStudio.md.
struct EnvironmentArchiveEntry
{
    std::string Name;
    std::vector<float> Linear; // RGB triplets, EnvironmentWidth x EnvironmentHeight
    std::array<float, 3> KeyDirection{-.42f, .72f, .55f}; // world frame
    float Strength = 1, KeyIntensity = .5f;
};
constexpr uint32_t EnvironmentWidth = 512, EnvironmentHeight = 256;

float DecodeSrgbChannel(uint8_t Value)
{
    const float Scaled = Value / 255.f;
    return Scaled <= .04045f ? Scaled / 12.92f : std::pow((Scaled + .055f) / 1.055f, 2.4f);
}
uint8_t EncodeSrgbChannel(float Value)
{
    const float Clamped = std::clamp(Value, 0.f, 1.f);
    const float Encoded = Clamped <= .0031308f ? Clamped * 12.92f : 1.055f * std::pow(Clamped, 1.f / 2.4f) - .055f;
    return uint8_t(std::lround(Encoded * 255.f));
}
float EnvironmentLuminance(float Red, float Green, float Blue)
{
    return .2126f * Red + .7152f * Green + .0722f * Blue;
}
// 📝 Inverse of the shader's equirect mapping: u from atan2(x, z), v from acos(y), +Y up.
std::array<float, 3> EnvironmentDirection(float U, float V)
{
    const float Polar = V * std::numbers::pi_v<float>;
    const float Azimuth = (U - .5f) * 2 * std::numbers::pi_v<float>;
    const float Sine = std::sin(Polar);
    return {Sine * std::sin(Azimuth), std::cos(Polar), Sine * std::cos(Azimuth)};
}
std::array<float, 3> SampleEnvironmentLinear(const EnvironmentArchiveEntry &Entry, std::array<float, 3> Direction)
{
    const float Length = std::sqrt(Direction[0] * Direction[0] + Direction[1] * Direction[1] + Direction[2] * Direction[2]);
    if (Length <= 1e-12f || Entry.Linear.empty()) return {0, 0, 0};
    const float X = Direction[0] / Length, Y = Direction[1] / Length, Z = Direction[2] / Length;
    float U = std::atan2(X, Z) / (2 * std::numbers::pi_v<float>) + .5f;
    const float V = std::acos(std::clamp(Y, -1.f, 1.f)) / std::numbers::pi_v<float>;
    U -= std::floor(U);
    const float Fx = U * EnvironmentWidth - .5f, Fy = std::clamp(V, 0.f, 1.f) * EnvironmentHeight - .5f;
    const int X0 = int(std::floor(Fx)), Y0 = int(std::floor(Fy));
    const float Tx = Fx - X0, Ty = Fy - Y0;
    std::array<float, 3> Result{0, 0, 0};
    for (int Row = 0; Row < 2; ++Row)
        for (int Column = 0; Column < 2; ++Column)
        {
            const int WrappedX = ((X0 + Column) % int(EnvironmentWidth) + int(EnvironmentWidth)) % int(EnvironmentWidth);
            const int ClampedY = std::clamp(Y0 + Row, 0, int(EnvironmentHeight) - 1);
            const float Weight = (Column ? Tx : 1 - Tx) * (Row ? Ty : 1 - Ty);
            const size_t Base = (size_t(ClampedY) * EnvironmentWidth + WrappedX) * 3;
            for (size_t Channel = 0; Channel < 3; ++Channel) Result[Channel] += Entry.Linear[Base + Channel] * Weight;
        }
    return Result;
}
std::string ConstructEnvironmentDisplayName(std::string Stem)
{
    size_t Start = 0;
    while (Start < Stem.size() && (std::isdigit(static_cast<unsigned char>(Stem[Start])) || Stem[Start] == '-' || Stem[Start] == '_')) ++Start;
    Stem.erase(0, Start);
    std::string Name;
    for (size_t Index = 0; Index < Stem.size(); ++Index)
    {
        const char Character = Stem[Index];
        if (Character == '-' || Character == '_') { if (!Name.empty() && Name.back() != ' ') Name += ' '; continue; }
        if (Index > 0 && std::isupper(static_cast<unsigned char>(Character)) && (std::islower(static_cast<unsigned char>(Stem[Index - 1])) ||
            (Index + 1 < Stem.size() && std::islower(static_cast<unsigned char>(Stem[Index + 1])) && std::isupper(static_cast<unsigned char>(Stem[Index - 1])))))
            Name += ' ';
        Name += Character;
    }
    for (size_t Index = 1; Index < Name.size(); ++Index)
        if (Name[Index - 1] != ' ') Name[Index] = static_cast<char>(std::tolower(static_cast<unsigned char>(Name[Index])));
    return Name.empty() ? std::string("Environment") : Name;
}
void AnalyseEnvironmentEntry(EnvironmentArchiveEntry &Entry)
{
    double WeightSum = 0, LuminanceSum = 0;
    std::vector<float> BlockPeak((EnvironmentWidth / 4) * (EnvironmentHeight / 4), 0);
    for (uint32_t Y = 0; Y < EnvironmentHeight; ++Y)
    {
        const float Weight = std::sin((Y + .5f) / EnvironmentHeight * std::numbers::pi_v<float>);
        for (uint32_t X = 0; X < EnvironmentWidth; ++X)
        {
            const size_t Base = (size_t(Y) * EnvironmentWidth + X) * 3;
            const float Luminance = EnvironmentLuminance(Entry.Linear[Base], Entry.Linear[Base + 1], Entry.Linear[Base + 2]);
            LuminanceSum += Luminance * Weight;
            WeightSum += Weight;
            float &Peak = BlockPeak[(Y / 4) * (EnvironmentWidth / 4) + X / 4];
            Peak = std::max(Peak, Luminance);
        }
    }
    const float Mean = float(LuminanceSum / std::max(WeightSum, 1e-9));
    std::sort(BlockPeak.begin(), BlockPeak.end());
    const float Peak = std::max(BlockPeak[size_t(BlockPeak.size() * .995)], 1e-4f);
    // 📝 Brightest-region centroid becomes the key light direction, so every archive (baked or
    // imported) lights its speculars from where its own panels actually are.
    double Centroid[3]{0, 0, 0}, CentroidWeight = 0;
    const float Threshold = Peak * .6f;
    for (uint32_t Y = 0; Y < EnvironmentHeight; ++Y)
    {
        const float V = (Y + .5f) / EnvironmentHeight;
        const float Weight = std::sin(V * std::numbers::pi_v<float>);
        for (uint32_t X = 0; X < EnvironmentWidth; ++X)
        {
            const size_t Base = (size_t(Y) * EnvironmentWidth + X) * 3;
            const float Excess = EnvironmentLuminance(Entry.Linear[Base], Entry.Linear[Base + 1], Entry.Linear[Base + 2]) - Threshold;
            if (Excess <= 0) continue;
            const double Contribution = double(Excess) * Excess * Weight;
            const auto Direction = EnvironmentDirection((X + .5f) / EnvironmentWidth, V);
            for (size_t Axis = 0; Axis < 3; ++Axis) Centroid[Axis] += Direction[Axis] * Contribution;
            CentroidWeight += Contribution;
        }
    }
    if (CentroidWeight > 1e-12)
    {
        std::array<float, 3> Direction{float(Centroid[0] / CentroidWeight), float(Centroid[1] / CentroidWeight),
                                       float(Centroid[2] / CentroidWeight)};
        Direction[1] = std::max(Direction[1], .12f); // key lights never sit below the floor line
        const float Length = std::sqrt(Direction[0] * Direction[0] + Direction[1] * Direction[1] + Direction[2] * Direction[2]);
        if (Length > 1e-6f) Entry.KeyDirection = {Direction[0] / Length, Direction[1] / Length, Direction[2] / Length};
    }
    // 📝 Normalize so the hemispheric mean lands at the studio reference level. The retired
    // analytic rig's effective diffuse ambient was ~0.2 including its key term; the first
    // map-lit build targeted 0.3 and big-panel archives flooded metallic paints toward white.
    Entry.Strength = std::clamp(.2f / std::max(Mean, 1e-4f), .15f, 4.f);
    Entry.KeyIntensity = std::clamp(.32f * Peak * Entry.Strength, .2f, 1.8f);
}
std::optional<EnvironmentArchiveEntry> DecodeEnvironmentArchive(const std::filesystem::path &Path)
{
    std::ifstream Input(Path, std::ios::binary | std::ios::ate);
    const auto Bytes = Input.tellg();
    if (!Input || Bytes <= 0 || Bytes > 67108864) return std::nullopt;
    std::vector<uint8_t> Encoded;
    Encoded.resize(size_t(Bytes));
    Input.seekg(0);
    if (!Input.read(reinterpret_cast<char *>(Encoded.data()), Bytes)) return std::nullopt;
    int Width = 0, Height = 0, Channels = 0;
    uint8_t *Pixels = stbi_load_from_memory(Encoded.data(), int(Encoded.size()), &Width, &Height, &Channels, 4);
    if (!Pixels || Width < 2 || Height < 1) { if (Pixels) stbi_image_free(Pixels); return std::nullopt; }
    EnvironmentArchiveEntry Entry;
    Entry.Name = ConstructEnvironmentDisplayName(Path.stem().string());
    Entry.Linear.assign(size_t(EnvironmentWidth) * EnvironmentHeight * 3, 0);
    // 📝 Area-approximating resample (4x4 bilinear taps per texel, horizontal wrap) into the shared
    // scene-linear sampling extent, whatever the archive's authored resolution was.
    const auto Source = [&](float U, float V, size_t Channel) {
        U -= std::floor(U);
        const float Fx = U * Width - .5f, Fy = std::clamp(V, 0.f, 1.f) * Height - .5f;
        const int X0 = int(std::floor(Fx)), Y0 = int(std::floor(Fy));
        const float Tx = Fx - X0, Ty = Fy - Y0;
        float Accumulation = 0;
        for (int Row = 0; Row < 2; ++Row)
            for (int Column = 0; Column < 2; ++Column)
            {
                const int WrappedX = ((X0 + Column) % Width + Width) % Width;
                const int ClampedY = std::clamp(Y0 + Row, 0, Height - 1);
                Accumulation += DecodeSrgbChannel(Pixels[(size_t(ClampedY) * Width + WrappedX) * 4 + Channel]) *
                                (Column ? Tx : 1 - Tx) * (Row ? Ty : 1 - Ty);
            }
        return Accumulation;
    };
    for (uint32_t Y = 0; Y < EnvironmentHeight; ++Y)
        for (uint32_t X = 0; X < EnvironmentWidth; ++X)
        {
            float Accumulation[3]{0, 0, 0};
            for (int TapY = 0; TapY < 4; ++TapY)
                for (int TapX = 0; TapX < 4; ++TapX)
                {
                    const float U = (X + (TapX + .5f) / 4) / EnvironmentWidth, V = (Y + (TapY + .5f) / 4) / EnvironmentHeight;
                    for (size_t Channel = 0; Channel < 3; ++Channel) Accumulation[Channel] += Source(U, V, Channel) / 16;
                }
            const size_t Base = (size_t(Y) * EnvironmentWidth + X) * 3;
            for (size_t Channel = 0; Channel < 3; ++Channel) Entry.Linear[Base + Channel] = Accumulation[Channel];
        }
    stbi_image_free(Pixels);
    AnalyseEnvironmentEntry(Entry);
    return Entry;
}
EnvironmentArchiveEntry SynthesizeFallbackEnvironment()
{
    // 📝 Neutral vertical gradient used only when no archive directory content is available, so
    // map lighting never hard-fails on missing content.
    EnvironmentArchiveEntry Entry;
    Entry.Name = "Studio gradient";
    Entry.Linear.assign(size_t(EnvironmentWidth) * EnvironmentHeight * 3, 0);
    for (uint32_t Y = 0; Y < EnvironmentHeight; ++Y)
    {
        const float Height = 1 - (Y + .5f) / EnvironmentHeight;
        const float Value = .04f + .5f * Height * Height;
        for (uint32_t X = 0; X < EnvironmentWidth; ++X)
        {
            const size_t Base = (size_t(Y) * EnvironmentWidth + X) * 3;
            Entry.Linear[Base] = Entry.Linear[Base + 1] = Entry.Linear[Base + 2] = Value;
        }
    }
    AnalyseEnvironmentEntry(Entry);
    return Entry;
}
MaterialTexturePyramid ConstructEnvironmentPyramid(const std::vector<EnvironmentArchiveEntry> &Entries)
{
    // 📝 Per-layer scene-linear mip chains re-encoded to sRGB bytes (the Radiance category selects
    // the sRGB GPU format), combined level-major like compiled material texture arrays.
    std::vector<std::vector<uint8_t>> Layers(Entries.size());
    std::vector<std::array<uint32_t, 2>> Extents;
    for (uint32_t Width = EnvironmentWidth, Height = EnvironmentHeight;;)
    {
        Extents.push_back({Width, Height});
        if (Width == 1 && Height == 1) break;
        Width = std::max(1u, Width / 2);
        Height = std::max(1u, Height / 2);
    }
    for (size_t Layer = 0; Layer < Entries.size(); ++Layer)
    {
        std::vector<float> Current = Entries[Layer].Linear;
        uint32_t Width = EnvironmentWidth, Height = EnvironmentHeight;
        for (const auto &Extent : Extents)
        {
            for (size_t Texel = 0; Texel < size_t(Width) * Height; ++Texel)
            {
                for (size_t Channel = 0; Channel < 3; ++Channel)
                    Layers[Layer].push_back(EncodeSrgbChannel(Current[Texel * 3 + Channel]));
                Layers[Layer].push_back(255);
            }
            if (Width == 1 && Height == 1) break;
            const uint32_t NextWidth = std::max(1u, Width / 2), NextHeight = std::max(1u, Height / 2);
            std::vector<float> Next(size_t(NextWidth) * NextHeight * 3, 0);
            for (uint32_t Y = 0; Y < NextHeight; ++Y)
                for (uint32_t X = 0; X < NextWidth; ++X)
                {
                    float Accumulation[3]{0, 0, 0};
                    int Taps = 0;
                    for (uint32_t Row = 0; Row < 2; ++Row)
                        for (uint32_t Column = 0; Column < 2; ++Column)
                        {
                            const uint32_t SourceX = std::min(X * 2 + Column, Width - 1), SourceY = std::min(Y * 2 + Row, Height - 1);
                            const size_t Base = (size_t(SourceY) * Width + SourceX) * 3;
                            for (size_t Channel = 0; Channel < 3; ++Channel) Accumulation[Channel] += Current[Base + Channel];
                            ++Taps;
                        }
                    const size_t Base = (size_t(Y) * NextWidth + X) * 3;
                    for (size_t Channel = 0; Channel < 3; ++Channel) Next[Base + Channel] = Accumulation[Channel] / Taps;
                }
            Current = std::move(Next);
            Width = NextWidth;
            Height = NextHeight;
        }
    }
    MaterialTexturePyramid Pyramid;
    Pyramid.Category = MaterialTextureCategory::Radiance;
    Pyramid.Layers = uint32_t(Entries.size());
    std::vector<size_t> LayerOffsets(Entries.size(), 0);
    for (const auto &Extent : Extents)
    {
        Pyramid.Levels.push_back({Extent[0], Extent[1], Pyramid.Rgba.size()});
        const size_t Bytes = size_t(Extent[0]) * Extent[1] * 4;
        for (size_t Layer = 0; Layer < Entries.size(); ++Layer)
        {
            Pyramid.Rgba.insert(Pyramid.Rgba.end(), Layers[Layer].begin() + LayerOffsets[Layer],
                                Layers[Layer].begin() + LayerOffsets[Layer] + Bytes);
            LayerOffsets[Layer] += Bytes;
        }
    }
    return Pyramid;
}
void RenderEnvironmentPreviewTile(const EnvironmentArchiveEntry &Entry, uint8_t *Target, uint32_t Stride)
{
    // 📝 Blender-style sphere swatch: polished grey ball (broad diffuse integral + tight reflection
    // average) over the environment seen behind it, integrated from the same linear pixels the
    // shader samples, so imported archives preview truthfully too.
    constexpr uint32_t Tile = MaterialLibraryRendering::EnvironmentPreviewTile;
    constexpr float DiscRadius = .9f;
    const auto Normalize = [](std::array<float, 3> Value) {
        const float Length = std::sqrt(Value[0] * Value[0] + Value[1] * Value[1] + Value[2] * Value[2]);
        return Length > 1e-12f ? std::array<float, 3>{Value[0] / Length, Value[1] / Length, Value[2] / Length}
                                : std::array<float, 3>{0, 0, 1};
    };
    for (uint32_t Y = 0; Y < Tile; ++Y)
        for (uint32_t X = 0; X < Tile; ++X)
        {
            const float Nx = (X + .5f) / Tile * 2 - 1, Ny = 1 - (Y + .5f) / Tile * 2;
            const auto Backdrop = SampleEnvironmentLinear(Entry, Normalize({Nx * .85f, Ny * .85f, -1}));
            std::array<float, 3> Colour{Backdrop[0] * .8f, Backdrop[1] * .8f, Backdrop[2] * .8f};
            const float RadiusSquared = Nx * Nx + Ny * Ny;
            if (RadiusSquared < 1)
            {
                const float Sx = Nx / DiscRadius, Sy = Ny / DiscRadius;
                const float SphereSquared = Sx * Sx + Sy * Sy;
                if (SphereSquared < 1)
                {
                    const std::array<float, 3> Normal{Sx, Sy, std::sqrt(1 - SphereSquared)};
                    const auto Tangent = Normalize({Normal[2], 0, -Normal[0]});
                    const std::array<float, 3> Bitangent{Normal[1] * Tangent[2] - Normal[2] * Tangent[1],
                                                         Normal[2] * Tangent[0] - Normal[0] * Tangent[2],
                                                         Normal[0] * Tangent[1] - Normal[1] * Tangent[0]};
                    std::array<float, 3> Diffuse{0, 0, 0};
                    float CosineSum = 0;
                    constexpr int DiffuseSamples = 40;
                    for (int Sample = 0; Sample < DiffuseSamples; ++Sample)
                    {
                        const float First = (Sample + .5f) / DiffuseSamples;
                        const float Second = std::fmod(Sample * .61803398875f + (X * 7 + Y * 13) * .0037f, 1.f);
                        const float Radius = std::sqrt(First), Angle = Second * 2 * std::numbers::pi_v<float>;
                        const std::array<float, 3> Local{Radius * std::cos(Angle), Radius * std::sin(Angle), std::sqrt(1 - First)};
                        const std::array<float, 3> Direction{Tangent[0] * Local[0] + Bitangent[0] * Local[1] + Normal[0] * Local[2],
                                                             Tangent[1] * Local[0] + Bitangent[1] * Local[1] + Normal[1] * Local[2],
                                                             Tangent[2] * Local[0] + Bitangent[2] * Local[1] + Normal[2] * Local[2]};
                        const auto Radiance = SampleEnvironmentLinear(Entry, Direction);
                        for (size_t Channel = 0; Channel < 3; ++Channel) Diffuse[Channel] += Radiance[Channel] * Local[2];
                        CosineSum += Local[2];
                    }
                    const std::array<float, 3> Reflection = Normalize(
                        {2 * Normal[2] * Normal[0], 2 * Normal[2] * Normal[1], 2 * Normal[2] * Normal[2] - 1});
                    std::array<float, 3> Specular{0, 0, 0};
                    constexpr int SpecularSamples = 12;
                    for (int Sample = 0; Sample < SpecularSamples; ++Sample)
                    {
                        const float Angle = Sample / float(SpecularSamples) * 2 * std::numbers::pi_v<float>;
                        const auto Jittered = Normalize({Reflection[0] + Tangent[0] * std::cos(Angle) * .06f + Bitangent[0] * std::sin(Angle) * .06f,
                                                         Reflection[1] + Tangent[1] * std::cos(Angle) * .06f + Bitangent[1] * std::sin(Angle) * .06f,
                                                         Reflection[2] + Tangent[2] * std::cos(Angle) * .06f + Bitangent[2] * std::sin(Angle) * .06f});
                        const auto Radiance = SampleEnvironmentLinear(Entry, Jittered);
                        for (size_t Channel = 0; Channel < 3; ++Channel) Specular[Channel] += Radiance[Channel] / SpecularSamples;
                    }
                    std::array<float, 3> Sphere{0, 0, 0};
                    for (size_t Channel = 0; Channel < 3; ++Channel)
                        Sphere[Channel] = (Diffuse[Channel] / std::max(CosineSum, 1e-6f)) * .4f + Specular[Channel] * .95f;
                    const float Edge = std::clamp((1 - std::sqrt(SphereSquared)) * Tile * DiscRadius * .5f, 0.f, 1.f);
                    for (size_t Channel = 0; Channel < 3; ++Channel)
                        Colour[Channel] = Colour[Channel] * (1 - Edge) + Sphere[Channel] * Edge;
                }
            }
            uint8_t *Pixel = Target + (size_t(Y) * Stride + X) * 4;
            for (size_t Channel = 0; Channel < 3; ++Channel)
            {
                const float Exposed = Colour[Channel] * Entry.Strength * 1.15f;
                Pixel[Channel] = EncodeSrgbChannel(Exposed / (1 + Exposed));
            }
            Pixel[3] = 255;
        }
}
std::vector<uint8_t> RenderEnvironmentPreviewAtlas(const std::vector<EnvironmentArchiveEntry> &Entries)
{
    constexpr uint32_t Tile = MaterialLibraryRendering::EnvironmentPreviewTile;
    constexpr uint32_t Columns = MaterialLibraryRendering::EnvironmentPreviewColumns;
    constexpr uint32_t Extent = (Tile + 2) * Columns;
    std::vector<uint8_t> Pixels(size_t(Extent) * Extent * 4, 0);
    const size_t Count = std::min<size_t>(Entries.size(), size_t(Columns) * Columns);
    for (size_t Index = 0; Index < Count; ++Index)
    {
        const uint32_t OriginX = uint32_t(Index % Columns) * (Tile + 2) + 1, OriginY = uint32_t(Index / Columns) * (Tile + 2) + 1;
        RenderEnvironmentPreviewTile(Entries[Index], Pixels.data() + (size_t(OriginY) * Extent + OriginX) * 4, Extent);
        // 📝 Replicate tile edges into the one-texel gutter so filtered sampling cannot bleed.
        for (uint32_t Step = 0; Step < Tile; ++Step)
        {
            const auto Copy = [&](uint32_t FromX, uint32_t FromY, uint32_t ToX, uint32_t ToY) {
                std::memcpy(Pixels.data() + (size_t(ToY) * Extent + ToX) * 4, Pixels.data() + (size_t(FromY) * Extent + FromX) * 4, 4);
            };
            Copy(OriginX + Step, OriginY, OriginX + Step, OriginY - 1);
            Copy(OriginX + Step, OriginY + Tile - 1, OriginX + Step, OriginY + Tile);
            Copy(OriginX, OriginY + Step, OriginX - 1, OriginY + Step);
            Copy(OriginX + Tile - 1, OriginY + Step, OriginX + Tile, OriginY + Step);
        }
    }
    return Pixels;
}
std::array<float, 3> EnvironmentKeyInDefaultView(const EnvironmentArchiveEntry &Entry)
{
    // 📝 The world-lock carry helper consumes directions authored in the default pose's view
    // frame; archive key directions are world-frame, so convert once here.
    const auto &View = ConstructMaterialPreviewProjection(MaterialPreviewConfiguration{}).ViewFromSurface;
    const auto &World = Entry.KeyDirection;
    std::array<float, 3> Converted{View[0] * World[0] + View[4] * World[1] + View[8] * World[2],
                                   View[1] * World[0] + View[5] * World[1] + View[9] * World[2],
                                   View[2] * World[0] + View[6] * World[1] + View[10] * World[2]};
    const float Length = std::sqrt(Converted[0] * Converted[0] + Converted[1] * Converted[1] + Converted[2] * Converted[2]);
    if (Length > 1e-9f) for (float &Component : Converted) Component /= Length;
    return Converted;
}
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SHARED RESIDENCY
//------------------------------------------------------------------------------------------------------------------------

struct MaterialLibraryRendering::RenderingAllocation
{
    SurfaceRasterPrograms Programs;
    SurfaceGeometryResidence Geometry;
    SurfaceRasterization Tile, Preview;
    MaterialThumbnailAtlas Atlas;
    struct MaterialGpuResidence
    {
        SurfaceMaterialResidence Textures;
        SurfaceScatteringResidence Parameters;
        // 📝 Environment array layers baked into this residence's own binding-3 image; preview
        // layer selection clamps to it so a rescan can never address a missing layer.
        uint32_t EnvironmentLayers = 1;
    };
    struct ThumbnailSlot
    {
        std::filesystem::path Path;
        uint64_t Generation = 0, Fingerprint = 0, Access = 0;
        uint64_t SourceDigest = 0;
        bool Ready = false, Pinned = false, Dirty = false;
        std::string Diagnostic;
    };
    struct PendingResidence
    {
        uint64_t Serial;
        std::shared_ptr<MaterialGpuResidence> Material;
    };
    struct PreviewRetirement
    {
        uint64_t Serial;
        std::shared_ptr<SurfaceScatteringResidence> Parameters;
    };
    std::vector<PreviewRetirement> PreviewRetirements;
    struct RetiredPreviewTarget
    {
        uint64_t Serial;
        std::unique_ptr<SurfaceRasterization> Target;
    };
    std::vector<RetiredPreviewTarget> RetiredPreviews;
    uint32_t PendingPreviewExtent = 0;
    MaterialScattering PreviewScattering;
    MaterialPreviewConfiguration Inspection;
    std::vector<EnvironmentArchiveEntry> Environments;
    std::vector<uint8_t> EnvironmentPreviewPixels;
    uint64_t EnvironmentGeneration = 0, SelectedEnvironmentGeneration = 0;
    std::shared_ptr<SurfaceScatteringResidence> PreviewParameters;
    // 📝 One completed spare parameter residence is recycled ping-pong style so continuous slider
    // edits stop allocating a fresh descriptor pool/set/buffer per input tick.
    std::shared_ptr<SurfaceScatteringResidence> SparePreviewParameters;
    uint64_t SparePreviewSerial = 0, CompletedSerial = 0;
    std::array<std::array<uint32_t, 2>, 5> ShapeIndexRanges{};
    std::vector<ThumbnailSlot> Slots;
    std::vector<PendingResidence> InFlight;
    std::filesystem::path Directory, Selection;
    std::shared_ptr<MaterialGpuResidence> Selected;
    uint64_t Clock = 0, GenerationClock = 0, SelectedFingerprint = 0, SubmittedSerial = 0, BakeCount = 0;
    bool PreviewDirty = false, PreviewReady = false, RecordedPreview = false;
    int RecordedSlot = -1;
    std::string Diagnostic;
    SurfaceNormalProjection Projection;
    // 📝 Disk cache state (see ConfigureAtlasCache). Entirely best-effort: a thrown exception from the
    // GPU readback/upload paths is always caught and degrades to the normal live bake, never propagated.
    std::filesystem::path CacheFile;
    uint64_t CacheFormatDigest = 0;
    bool CacheConfigured = false;
    std::vector<ThumbnailAtlasSlotCacheEntry> PendingCacheManifest;
    std::vector<uint8_t> PendingCachePixels;
    bool PendingCacheUpload = false, RecordedCacheUpload = false;
    bool CacheDirty = false, CacheReadbackPending = false, CacheSettled = true;

    const EnvironmentArchiveEntry &EnvironmentAt(uint32_t Index) const
    {
        static const EnvironmentArchiveEntry Fallback = SynthesizeFallbackEnvironment();
        if (Environments.empty()) return Fallback;
        return Environments[std::min<size_t>(Index, Environments.size() - 1)];
    }
    std::shared_ptr<MaterialGpuResidence> CompileResidence(const CompiledSurfaceMaterial &Material)
    {
        auto Residence = std::make_shared<MaterialGpuResidence>();
        Residence->EnvironmentLayers = uint32_t(std::max<size_t>(Environments.size(), 1));
        auto Scattering = Material.Scattering;
        Scattering.PreviewConfiguration = {1,0,0,0};
        // 📝 Tiles light from the first (studio) archive: radiance comes from the map, strength
        // rides in Horizon.w and the derived key light matches the default-pose preview.
        const auto &Environment = EnvironmentAt(0);
        Scattering.Illumination.EnvironmentHorizon = {0,0,0,Environment.Strength};
        Scattering.Illumination.EnvironmentZenith = {0,0,0,0};
        const auto TileKey = EnvironmentKeyInDefaultView(Environment);
        Scattering.Illumination.LightDirectionIntensity = {TileKey[0],TileKey[1],TileKey[2],Environment.KeyIntensity};
        Scattering.Illumination.LightColourExposure = {1,1,1,1};
        // 📝 Thumbnails share the Alloy capture rig (34° perspective, per-pixel view); the fixed tile
        // projection below is the same default camera, so cards match the capture reference.
        Scattering.Illumination.Configuration[2] = 1;
        Scattering.Illumination.ViewFromSurface =
            ConstructMaterialPreviewProjection(MaterialPreviewConfiguration{}).ViewFromSurface;
        Residence->Textures.Initialize(Programs, Material.Textures, Material.AuthoringMaps);
        Residence->Parameters.Initialize(Programs, Scattering);
        return Residence;
    }
    void ReconfigurePreview()
    {
        if (!Selected) return;
        auto Scattering = PreviewScattering;
        // 📝 Preview lighting is owned by the selected environment archive, never inherited from
        // arbitrary authored source lighting. The map is sampled in the fixed world frame inside
        // the shader; layer selection clamps to both the catalogue and the layers this residence
        // actually uploaded, so a rescan can never address a missing layer.
        const uint32_t CatalogueLayers = uint32_t(std::max<size_t>(Environments.size(), 1));
        const uint32_t ResidentLayers = Selected ? std::max(Selected->EnvironmentLayers, 1u) : CatalogueLayers;
        const uint32_t EnvironmentIndex = std::min({Inspection.Environment, CatalogueLayers - 1, ResidentLayers - 1});
        const auto &Environment = EnvironmentAt(EnvironmentIndex);
        Scattering.PreviewConfiguration = {uint32_t(Inspection.Shape) + 1, 0, EnvironmentIndex, 0};
        Scattering.Illumination.ViewFromSurface = ConstructMaterialPreviewProjection(Inspection).ViewFromSurface;
        Scattering.Illumination.Configuration[2] = 1;
        Scattering.Illumination.LightColourExposure = {1,1,1,Inspection.Exposure};
        Scattering.Illumination.EnvironmentHorizon = {0,0,0,Environment.Strength * Inspection.LightIntensity};
        Scattering.Illumination.EnvironmentZenith = {0,0,0,0};
        const auto KeyReference = EnvironmentKeyInDefaultView(Environment);
        Scattering.Illumination.LightDirectionIntensity = {KeyReference[0],KeyReference[1],KeyReference[2],
            Environment.KeyIntensity * Inspection.LightIntensity};
        // 📝 Carry the studio rig from the default pose into the orbiting view: the key light and
        // sky stay fixed in the world while the camera moves, matching OrbitControls staging.
        const auto LightDirection = ConstructMaterialPreviewLightDirection(Inspection,
            {Scattering.Illumination.LightDirectionIntensity[0],
             Scattering.Illumination.LightDirectionIntensity[1],
             Scattering.Illumination.LightDirectionIntensity[2]});
        Scattering.Illumination.LightDirectionIntensity = {LightDirection[0], LightDirection[1],
            LightDirection[2], Scattering.Illumination.LightDirectionIntensity[3]};
        const auto EnvironmentUp = ConstructMaterialPreviewEnvironmentUp(Inspection);
        Scattering.Illumination.EnvironmentUp = {EnvironmentUp[0], EnvironmentUp[1], EnvironmentUp[2], 0};
        std::shared_ptr<SurfaceScatteringResidence> Replacement;
        if (SparePreviewParameters && SparePreviewSerial <= CompletedSerial)
        {
            // 📝 Every submission that sampled the spare has fenced; reuse its object instead of
            // allocating another pool/set/buffer for this edit tick.
            Replacement = std::move(SparePreviewParameters);
            Replacement->Finalize();
        }
        else Replacement = std::make_shared<SurfaceScatteringResidence>();
        Replacement->Initialize(Programs, Scattering);
        if (PreviewParameters)
        {
            if (!SparePreviewParameters)
            {
                SparePreviewParameters = std::move(PreviewParameters);
                SparePreviewSerial = SubmittedSerial;
            }
            else PreviewRetirements.push_back({SubmittedSerial, std::move(PreviewParameters)});
        }
        PreviewParameters = std::move(Replacement);
        PreviewDirty = true;
        PreviewReady = false;
    }
    CompiledSurfaceMaterial DecodeLibraryMaterial(const std::filesystem::path &RelativePath)
    {
        const auto Path = std::filesystem::canonical(Directory / RelativePath);
        const auto Contained = Path.lexically_relative(Directory);
        if (Contained.empty() || Contained.is_absolute() || *Contained.begin() == "..")
            throw std::runtime_error("Thumbnail material escapes ContentDirectory.");
        std::ifstream Input(Path, std::ios::binary | std::ios::ate);
        const auto Bytes = Input.tellg();
        if (!Input || Bytes < 0 || Bytes > 1048576) throw std::runtime_error("Thumbnail material source missing or oversized.");
        std::string Source(size_t(Bytes), '\0');
        Input.seekg(0);
        if (!Input.read(Source.data(), Bytes)) throw std::runtime_error("Thumbnail source read failed.");
        auto Definition = DecodeSurfaceMaterial(Source);
        EnsureProceduralChannelBakes(Definition, 256);
        return CompileSurfaceMaterial(Definition, Directory);
    }
};

MaterialLibraryRendering::MaterialLibraryRendering() = default;
MaterialLibraryRendering::~MaterialLibraryRendering() = default;
void MaterialLibraryRendering::Initialize(const VulkanRasterAccess &Access, const std::filesystem::path &ShaderDirectory)
{
    if (Allocation) throw std::runtime_error("Material library rendering already initialized.");
    DiagnosticJournal::Append("MaterialGPU", "Initialize shader directory: " + DiagnosticJournal::EncodePath(ShaderDirectory));
    auto Resident = std::make_unique<RenderingAllocation>();
    const std::array<const char *, 10> Names{"CadSurfaceVertex", "CadSurfaceFragment", "CadEdgeVertex", "CadEdgeFragment",
        "MatCapSurfaceVertex", "MatCapSurfaceFragment", "PreviewPbrSurfaceVertex", "PreviewPbrSurfaceFragment", "MaterialSurfaceVertex", "MaterialSurfaceFragment"};
    std::array<std::vector<uint32_t>, 10> Words;
    for (size_t Index = 0; Index < Names.size(); ++Index)
    {
        Words[Index] = DecodePreviewShader(ShaderDirectory / (std::string(Names[Index]) + ".spv"));
        DiagnosticJournal::Append("Shader", std::string(Names[Index]) + " bytes=" + std::to_string(Words[Index].size() * 4));
    }
    Resident->Programs.Initialize(Access, {{Words[0],"main"},{Words[1],"main"},{Words[2],"main"},{Words[3],"main"},
        {Words[4],"main"},{Words[5],"main"},{Words[6],"main"},{Words[7],"main"},{Words[8],"main"},{Words[9],"main"}});
    auto Inspection = ConstructMaterialInspectionGeometryWithRanges();
    Resident->ShapeIndexRanges = Inspection.ShapeIndexRanges;
    Resident->Geometry.Initialize(Access, Inspection.Geometry);
    Resident->Tile.Initialize(Resident->Programs, Resident->Geometry, 256, 256);
    Resident->Preview.Initialize(Resident->Programs, Resident->Geometry, 512, 512);
    // 📝 Tiles always bake the shader ball; the preview re-selects its shape range per recording.
    Resident->Tile.ConfigureIndexRange(Inspection.ShapeIndexRanges[0][0], Inspection.ShapeIndexRanges[0][1]);
    Resident->Preview.ConfigureIndexRange(Inspection.ShapeIndexRanges[0][0], Inspection.ShapeIndexRanges[0][1]);
    Resident->Tile.ConfigureTransparentBackground(true);
    Resident->Preview.ConfigureTransparentBackground(true);
    Resident->Atlas.Initialize(Access, 256, 8);
    Resident->Slots.resize(Resident->Atlas.QueryCapacity());
    Resident->Projection.ClipFromSurface =
        ConstructMaterialPreviewProjection(MaterialPreviewConfiguration{}).ClipFromSurface;
    Allocation = std::move(Resident);
}
void MaterialLibraryRendering::ConfigureAtlasCache(const std::filesystem::path &File, uint64_t FormatDigest)
{
    if (!Allocation) throw std::runtime_error("Material rendering is not initialized.");
    auto &Rendering = *Allocation;
    Rendering.CacheFile = File;
    // 📝 Fold the atlas's own layout into the digest so a future tile/column size change (or cache
    // format revision) can never be misread as a matching cache -- it folds through to a clean miss.
    Rendering.CacheFormatDigest = CombineHash(CombineHash(FormatDigest, Rendering.Atlas.QueryExtent()), Rendering.Atlas.QueryCapacity());
    Rendering.CacheConfigured = !File.empty();
}
bool MaterialLibraryRendering::QueryAtlasCacheSettled() const noexcept
{
    return Allocation && Allocation->CacheSettled;
}
void MaterialLibraryRendering::ConfigureEnvironments(const std::filesystem::path &Directory)
{
    if (!Allocation) throw std::runtime_error("Material rendering is not initialized.");
    auto &Rendering = *Allocation;
    std::vector<std::filesystem::path> Archives;
    std::error_code Error;
    if (!Directory.empty() && std::filesystem::is_directory(Directory, Error))
        for (const auto &Candidate : std::filesystem::directory_iterator(Directory, Error))
        {
            if (Error) break;
            if (!Candidate.is_regular_file(Error)) continue;
            std::string Extension = Candidate.path().extension().string();
            for (char &Character : Extension) Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
            if (Extension == ".png") Archives.push_back(Candidate.path());
        }
    std::sort(Archives.begin(), Archives.end());
    // 📝 The studio archive leads the catalogue (default preview + thumbnail lighting); the rest
    // follow filename order, which is also the dropdown order.
    std::stable_partition(Archives.begin(), Archives.end(), [](const std::filesystem::path &Path) {
        std::string Stem = Path.stem().string();
        for (char &Character : Stem) Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
        const size_t Start = Stem.find_first_not_of("0123456789-_ ");
        return Stem.compare(Start == std::string::npos ? 0 : Start, 6, "studio") == 0;
    });
    std::vector<EnvironmentArchiveEntry> Entries;
    for (const auto &Path : Archives)
    {
        // 📝 The shared pyramid contract bounds texture arrays at 8 layers; the catalogue
        // caps there too (preview atlas and shader addressing agree).
        if (Entries.size() >= 8) break;
        if (auto Decoded = DecodeEnvironmentArchive(Path)) Entries.push_back(std::move(*Decoded));
        else DiagnosticJournal::Append("EnvironmentArchive", "decode refused: " + DiagnosticJournal::EncodePath(Path));
    }
    if (Entries.empty()) Entries.push_back(SynthesizeFallbackEnvironment());
    Rendering.Programs.ConfigureEnvironment(ConstructEnvironmentPyramid(Entries));
    // 📝 Free the 512x256 float fields once both consumers (GPU pyramid, preview atlas) are
    // built; only the derived lighting facts stay resident per entry.
    Rendering.EnvironmentPreviewPixels = RenderEnvironmentPreviewAtlas(Entries);
    for (auto &Entry : Entries) { Entry.Linear.clear(); Entry.Linear.shrink_to_fit(); }
    Rendering.Environments = std::move(Entries);
    ++Rendering.EnvironmentGeneration;
    const uint32_t CatalogueLayers = uint32_t(std::max<size_t>(Rendering.Environments.size(), 1));
    Rendering.Inspection.Environment = std::min(Rendering.Inspection.Environment, CatalogueLayers - 1);
    // 📝 Lighting is baked into resident tiles and into each residence's own environment image:
    // clear source digests so tiles rebake against the new rig instead of re-authenticating.
    for (auto &Slot : Rendering.Slots) if (!Slot.Path.empty()) { Slot.Dirty = true; Slot.SourceDigest = 0; }
    Rendering.ReconfigurePreview();
    Rendering.PreviewDirty = true;
    DiagnosticJournal::Append("EnvironmentArchive", "catalogue=" + std::to_string(Rendering.Environments.size()) +
        " generation=" + std::to_string(Rendering.EnvironmentGeneration));
}
std::vector<std::string> MaterialLibraryRendering::QueryEnvironmentNames() const
{
    std::vector<std::string> Names;
    if (!Allocation) return Names;
    for (const auto &Entry : Allocation->Environments) Names.push_back(Entry.Name);
    return Names;
}
EnvironmentPreviewAtlas MaterialLibraryRendering::QueryEnvironmentPreviews() const noexcept
{
    if (!Allocation) return {};
    return {Allocation->EnvironmentPreviewPixels, Allocation->EnvironmentGeneration,
            uint32_t(Allocation->Environments.size())};
}
void MaterialLibraryRendering::RequestLibrary(const std::filesystem::path &Directory, const std::vector<std::filesystem::path> &Visible)
{
    if (!Allocation) throw std::runtime_error("Material rendering is not initialized.");
    auto &Rendering = *Allocation;
    if (Directory.empty()) return;
    const auto Canonical = std::filesystem::canonical(Directory);
    if (Canonical != Rendering.Directory)
    {
        Rendering.Directory = Canonical;
        for (auto &Slot : Rendering.Slots) Slot = {};
        Rendering.Selection.clear();
        if (Rendering.Selected) Rendering.InFlight.push_back({Rendering.SubmittedSerial, std::move(Rendering.Selected)});
        Rendering.SelectedFingerprint = 0;
        Rendering.PreviewReady = Rendering.PreviewDirty = false;
        // 📝 A new library identity: try a disk-cached atlas before baking anything live. A hit stages
        // the whole atlas for one wholesale GPU upload below and lets per-slot digests decide which
        // individual tiles (if any) still need a live bake; a miss just falls through unchanged.
        Rendering.PendingCacheManifest.clear();
        Rendering.PendingCachePixels.clear();
        Rendering.PendingCacheUpload = false;
        Rendering.CacheDirty = false;
        Rendering.CacheSettled = true;
        if (Rendering.CacheConfigured)
        {
            ThumbnailAtlasSlotCacheRecord Record;
            const auto Extent = Rendering.Atlas.QueryExtent();
            if (LoadThumbnailAtlasSlotCache(Rendering.CacheFile, Rendering.CacheFormatDigest, Extent, Extent, Record))
            {
                Rendering.PendingCacheManifest = std::move(Record.Slots);
                Rendering.PendingCachePixels = std::move(Record.Rgba);
                Rendering.PendingCacheUpload = true;
            }
        }
    }
    ++Rendering.Clock;
    // 📝 Pin all existing visible slots before allocation, so a later card cannot lose its tile mid-layout.
    for (auto &Slot : Rendering.Slots)
        Slot.Pinned = std::find(Visible.begin(), Visible.end(), Slot.Path) != Visible.end();
    const auto ApplyCacheHit = [&](RenderingAllocation::ThumbnailSlot &Slot)
    {
        if (!Rendering.PendingCacheUpload) return;
        const auto SlotIndex = size_t(&Slot - Rendering.Slots.data());
        if (SlotIndex >= Rendering.PendingCacheManifest.size()) return;
        const auto &Entry = Rendering.PendingCacheManifest[SlotIndex];
        if (Entry.RelativePath.empty() || Entry.RelativePath != Slot.Path.generic_string()) return;
        const auto Digest = ComputeMaterialSourceDigest(Rendering.Directory, Slot.Path);
        if (!Digest || Digest != Entry.Digest) return;
        // 📝 This slot's content has not changed since the cache was written: trust the wholesale
        // upload about to be staged below instead of re-rendering it through the PBR pipeline.
        Slot.Dirty = false;
        Slot.Ready = true;
        Slot.SourceDigest = Digest;
    };
    for (const auto &Path : Visible)
    {
        auto Existing = std::find_if(Rendering.Slots.begin(), Rendering.Slots.end(), [&](const auto &Slot) { return Slot.Path == Path; });
        if (Existing == Rendering.Slots.end())
        {
            Existing = std::min_element(Rendering.Slots.begin(), Rendering.Slots.end(), [](const auto &Left, const auto &Right)
            {
                if (Left.Pinned != Right.Pinned) return !Left.Pinned;
                return Left.Access < Right.Access;
            });
            if (Existing->Pinned) continue;
            const auto Generation = ++Rendering.GenerationClock;
            *Existing = {};
            Existing->Path = Path;
            Existing->Generation = Generation;
            Existing->Dirty = Existing->Pinned = true;
            ApplyCacheHit(*Existing);
        }
        Existing->Access = Rendering.Clock;
    }
}
void MaterialLibraryRendering::ConfigureSelection(const std::filesystem::path &Path, const CompiledSurfaceMaterial &Material)
{
    if (!Allocation) throw std::runtime_error("Material rendering is not initialized.");
    auto &Rendering = *Allocation;
    const auto Digest = ComputeMaterialFingerprint(Material);
    if (Rendering.Selection == Path && Rendering.SelectedFingerprint == Digest &&
        Rendering.SelectedEnvironmentGeneration == Rendering.EnvironmentGeneration) return;
    Rendering.SelectedEnvironmentGeneration = Rendering.EnvironmentGeneration;
    auto Replacement = Rendering.CompileResidence(Material);
    if (Rendering.Selected) Rendering.InFlight.push_back({Rendering.SubmittedSerial, std::move(Rendering.Selected)});
    Rendering.Selected = std::move(Replacement);
    Rendering.PreviewScattering = Material.Scattering;
    Rendering.ReconfigurePreview();
    DiagnosticJournal::Append("MaterialSelection", DiagnosticJournal::EncodePath(Path) + " fingerprint=" + std::to_string(Digest));
    DiagnosticJournal::Append("MaterialMaps", DiagnosticJournal::EncodePath(Path) + " " + DescribeMaterialMaps(Material));
    Rendering.SelectedFingerprint = Digest;
    Rendering.Selection = Path;
    Rendering.PreviewDirty = true;
    // 📝 Selection edits deliberately do NOT dirty the catalogue tile: the atlas is baked content and
    // only re-renders when the material is saved (InvalidateThumbnail) or the library is refreshed.
}
void MaterialLibraryRendering::ConfigurePreview(const MaterialPreviewConfiguration &Configuration)
{
    if (!Allocation || !ValidateMaterialPreviewConfiguration(Configuration)) throw std::runtime_error("Invalid material inspection configuration.");
    auto &Rendering = *Allocation;
    auto Clamped = Configuration;
    const uint32_t CatalogueLayers = uint32_t(std::max<size_t>(Rendering.Environments.size(), 1));
    Clamped.Environment = std::min(Clamped.Environment, CatalogueLayers - 1);
    if (Rendering.Inspection == Clamped) return;
    Rendering.Inspection = Clamped;
    Rendering.ReconfigurePreview();
    DiagnosticJournal::Append("PreviewConfiguration", "shape=" + std::to_string(uint32_t(Configuration.Shape)) +
        " environment=" + std::to_string(Configuration.Environment) + " zoom=" + std::to_string(Configuration.Zoom) +
        " exposure=" + std::to_string(Configuration.Exposure) +
        " pan=" + std::to_string(Configuration.PanX) + "," + std::to_string(Configuration.PanY) +
        " orbit=" + std::to_string(Configuration.Yaw) + "," + std::to_string(Configuration.Pitch));
}
MaterialPreviewConfiguration MaterialLibraryRendering::QueryPreviewConfiguration() const
{
    return Allocation ? Allocation->Inspection : MaterialPreviewConfiguration{};
}
void MaterialLibraryRendering::ConfigurePreviewExtent(uint32_t Extent)
{
    if (!Allocation) throw std::runtime_error("Material rendering is not initialized.");
    auto &Rendering = *Allocation;
    const uint32_t Clamped = std::clamp(Extent, 256u, 2048u);
    if (Clamped != Rendering.Preview.QueryExtent().width) Rendering.PendingPreviewExtent = Clamped;
}
uint32_t MaterialLibraryRendering::QueryPreviewExtent() const noexcept
{
    return Allocation ? Allocation->Preview.QueryExtent().width : 0;
}
void MaterialLibraryRendering::ConstructCommands(VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial)
{
    if (!Allocation) throw std::runtime_error("Material rendering is not initialized.");
    auto &Rendering = *Allocation;
    Rendering.CompletedSerial = std::max(Rendering.CompletedSerial, CompletedSerial);
    // 📝 Pure-CPU poll: safe every call regardless of what else happens this frame, never touches Commands.
    if (Rendering.CacheReadbackPending && Rendering.Atlas.QueryReadbackReady(Rendering.CompletedSerial))
    {
        try
        {
            auto Pixels = Rendering.Atlas.RetrieveReadback(Rendering.CompletedSerial);
            std::vector<ThumbnailAtlasSlotCacheEntry> Manifest;
            Manifest.reserve(Rendering.Slots.size());
            for (const auto &Slot : Rendering.Slots)
                Manifest.push_back({Slot.Path.empty() ? std::string() : Slot.Path.generic_string(), Slot.Path.empty() ? 0 : Slot.SourceDigest});
            const auto Extent = Rendering.Atlas.QueryExtent();
            SaveThumbnailAtlasSlotCache(Rendering.CacheFile, Rendering.CacheFormatDigest, Extent, Extent, Pixels, Manifest);
            PruneThumbnailAtlasCacheDirectory(Rendering.CacheFile.parent_path());
            DiagnosticJournal::Append("MaterialAtlasCache", "saved " + DiagnosticJournal::EncodePath(Rendering.CacheFile));
        }
        catch (const std::exception &Failure)
        {
            DiagnosticJournal::Append("MaterialAtlasCache", std::string("save skipped: ") + Failure.what());
        }
        Rendering.CacheReadbackPending = false;
        Rendering.CacheDirty = false;
        Rendering.CacheSettled = true;
    }
    std::erase_if(Rendering.PreviewRetirements, [&](const auto &Residence) { return Residence.Serial <= CompletedSerial; });
    std::erase_if(Rendering.InFlight, [&](const auto &Residence) { return Residence.Serial <= CompletedSerial; });
    std::erase_if(Rendering.RetiredPreviews, [&](const auto &Retired) { return Retired.Serial <= CompletedSerial; });
    if (Rendering.PendingPreviewExtent && Rendering.PendingPreviewExtent != Rendering.Preview.QueryExtent().width)
    {
        auto Retired = std::make_unique<SurfaceRasterization>(std::move(Rendering.Preview));
        Rendering.RetiredPreviews.push_back({SubmissionSerial, std::move(Retired)});
        Rendering.Preview.Initialize(Rendering.Programs, Rendering.Geometry, Rendering.PendingPreviewExtent,
            Rendering.PendingPreviewExtent);
        Rendering.Preview.ConfigureTransparentBackground(true);
        const auto &ReplacementRange = Rendering.ShapeIndexRanges[size_t(Rendering.Inspection.Shape)];
        Rendering.Preview.ConfigureIndexRange(ReplacementRange[0], ReplacementRange[1]);
        DiagnosticJournal::Append("PreviewExtent", "preview target=" + std::to_string(Rendering.PendingPreviewExtent));
        Rendering.PendingPreviewExtent = 0;
        Rendering.PreviewDirty = true;
        Rendering.PreviewReady = false;
    }
    Rendering.SubmittedSerial = SubmissionSerial;
    Rendering.RecordedSlot = -1;
    Rendering.RecordedPreview = false;
    Rendering.RecordedCacheUpload = false;
    if (Rendering.PendingCacheUpload)
    {
        // 📝 A disk-cached atlas is staged wholesale in one upload (same primitive the Pattern gallery
        // already uses), bypassing every individually-matching slot's PBR render entirely. Any mismatched
        // slots stay Dirty from RequestLibrary and bake live afterwards, naturally overwriting just their
        // own tile region once the upload below lands. Already-validated (magic/digest/dimensions) by
        // LoadThumbnailAtlasSlotCache, so a throw here is a genuine GPU failure -- propagate it like any
        // other recording failure in this function rather than silently leaving stale pixels on screen.
        Rendering.Atlas.StagePixels(Rendering.PendingCachePixels);
        Rendering.Atlas.ConstructUpload(Commands);
        Rendering.RecordedCacheUpload = true;
        DiagnosticJournal::Append("MaterialAtlasCache", "loaded " + DiagnosticJournal::EncodePath(Rendering.CacheFile));
        Rendering.PendingCacheUpload = false;
        Rendering.PendingCachePixels.clear();
        return;
    }
    if (Rendering.Selected && Rendering.PreviewDirty)
    {
        auto Projection = Rendering.Projection;
        const auto Camera = ConstructMaterialPreviewProjection(Rendering.Inspection);
        Projection.ClipFromSurface = Camera.ClipFromSurface;
        const auto &View = Camera.ViewFromSurface;
        Projection.ViewNormalRow0 = {View[0],View[4],View[8],0};
        Projection.ViewNormalRow1 = {View[1],View[5],View[9],0};
        Projection.ViewNormalRow2 = {View[2],View[6],View[10],0};
        // 📝 Only the selected specimen is drawn; the other four share this upload 16 units apart and
        // otherwise swing into frame while orbiting (the reported "two shapes" viewport defect).
        const auto &Range = Rendering.ShapeIndexRanges[size_t(Rendering.Inspection.Shape)];
        Rendering.Preview.ConfigureIndexRange(Range[0], Range[1]);
        Rendering.Preview.ConstructCommands(Commands, Projection, Rendering.Selected->Textures, *Rendering.PreviewParameters);
        DiagnosticJournal::Append("PreviewSubmit", "serial=" + std::to_string(SubmissionSerial));
        Rendering.RecordedPreview = true;
        // 📝 First-use geometry/texture uploads belong to one target recording until accepted submission.
        // Alternate preview work and tile work rather than recording the same pending transfers twice.
        return;
    }
    const auto Dirty = std::find_if(Rendering.Slots.begin(), Rendering.Slots.end(), [](const auto &Slot) { return Slot.Dirty && Slot.Pinned; });
    if (Dirty == Rendering.Slots.end())
    {
        // 📝 Nothing left to bake this pass: if something changed since the last save (CacheDirty implies
        // at least one tile write already completed, so the atlas is guaranteed to already be in its
        // settled SHADER_READ_ONLY_OPTIMAL layout), capture one wholesale readback for the disk cache.
        if (Rendering.CacheConfigured && Rendering.CacheDirty && !Rendering.CacheReadbackPending)
        {
            Rendering.Atlas.ConstructReadback(Commands, SubmissionSerial);
            Rendering.CacheReadbackPending = true;
            Rendering.CacheSettled = false;
        }
        return;
    }
    // 📝 Baked-atlas contract: an already-baked tile whose authored inputs are unchanged keeps its
    // pixels. The digest costs one small file read instead of a full decode/procedural-bake/compile.
    const auto SourceDigest = Dirty->Ready ? ComputeMaterialSourceDigest(Rendering.Directory, Dirty->Path) : 0;
    if (Dirty->Ready && SourceDigest && SourceDigest == Dirty->SourceDigest)
    {
        Dirty->Dirty = false;
        Dirty->Diagnostic.clear();
        return;
    }
    std::shared_ptr<RenderingAllocation::MaterialGpuResidence> Material;
    uint64_t Digest = Rendering.SelectedFingerprint;
    std::optional<CompiledSurfaceMaterial> Compiled;
    if (Dirty->Path == Rendering.Selection && Rendering.Selected) Material = Rendering.Selected;
    else
    {
        try
        {
            Compiled = Rendering.DecodeLibraryMaterial(Dirty->Path);
            Digest = ComputeMaterialFingerprint(*Compiled);
        }
        catch (const std::exception &Failure)
        {
            Dirty->Dirty = false;
            Dirty->Diagnostic = Rendering.Diagnostic = Failure.what();
            DiagnosticJournal::Append("MaterialTileRefused", DiagnosticJournal::EncodePath(Dirty->Path) + ": " + Rendering.Diagnostic);
            return;
        }
    }
    if (Dirty->Ready && Digest == Dirty->Fingerprint)
    {
        Dirty->Dirty = false;
        Dirty->Diagnostic.clear();
        if (SourceDigest) Dirty->SourceDigest = SourceDigest;
        return;
    }
    // ⚠️ GPU allocation/recording failures propagate to the application's severe-failure presentation.
    if (!Material) Material = Rendering.CompileResidence(*Compiled);
    Rendering.InFlight.push_back({SubmissionSerial, Material});
    Rendering.Tile.ConstructCommands(Commands, Rendering.Projection, Material->Textures, Material->Parameters);
    Rendering.Atlas.ConstructCopy(Commands, Rendering.Tile, uint32_t(Dirty - Rendering.Slots.begin()));
    Dirty->Fingerprint = Digest;
    Dirty->SourceDigest = SourceDigest ? SourceDigest : ComputeMaterialSourceDigest(Rendering.Directory, Dirty->Path);
    Rendering.RecordedSlot = int(Dirty - Rendering.Slots.begin());
    DiagnosticJournal::Append("AtlasBake", DiagnosticJournal::EncodePath(Dirty->Path) + " slot=" + std::to_string(Rendering.RecordedSlot));
    Dirty->Diagnostic.clear();
    Rendering.Diagnostic.clear();
}
void MaterialLibraryRendering::ConfirmSubmission() noexcept
{
    if (!Allocation) return;
    auto &Rendering = *Allocation;
    if (Rendering.RecordedPreview)
    {
        Rendering.Preview.ConfirmSubmission();
        Rendering.PreviewReady = true;
        Rendering.PreviewDirty = false;
    }
    if (Rendering.RecordedSlot >= 0)
    {
        Rendering.Tile.ConfirmSubmission();
        Rendering.Atlas.ConfirmSubmission();
        auto &Slot = Rendering.Slots[size_t(Rendering.RecordedSlot)];
        ++Rendering.BakeCount;
        Slot.Ready = true;
        Slot.Dirty = false;
        // 📝 A real PBR bake happened: the on-disk cache (if configured) is now stale and should be
        // refreshed once every pinned slot settles again (see the readback trigger in ConstructCommands).
        if (Rendering.CacheConfigured) { Rendering.CacheDirty = true; Rendering.CacheSettled = false; }
    }
    if (Rendering.RecordedCacheUpload) Rendering.Atlas.ConfirmSubmission();
}
MaterialThumbnailSelection MaterialLibraryRendering::QueryThumbnail(const std::filesystem::path &Path) const
{
    if (!Allocation) return {};
    const auto &Rendering = *Allocation;
    const auto Slot = std::find_if(Rendering.Slots.begin(), Rendering.Slots.end(), [&](const auto &Entry) { return Entry.Path == Path; });
    if (Slot == Rendering.Slots.end() || !Slot->Ready) return {};
    return {Slot->Generation, Rendering.Atlas.QueryUv(uint32_t(Slot - Rendering.Slots.begin())), true};
}
void MaterialLibraryRendering::InvalidateThumbnails() noexcept
{
    // 📝 Marks every resident tile for source re-authentication; unchanged inputs keep their baked
    // pixels via the source digest, so a library refresh no longer rebakes the whole atlas.
    if (Allocation) for (auto &Slot : Allocation->Slots) if (!Slot.Path.empty()) Slot.Dirty = true;
}
void MaterialLibraryRendering::InvalidateThumbnail(const std::filesystem::path &RelativePath) noexcept
{
    // 📝 The only edit-path rebake: the material was saved to disk, so its tile (if resident) re-bakes.
    if (!Allocation) return;
    for (auto &Slot : Allocation->Slots)
        if (Slot.Path == RelativePath)
        {
            Slot.Dirty = true;
            Slot.SourceDigest = 0;
        }
}
bool MaterialLibraryRendering::AuthenticateThumbnail(const std::filesystem::path &Path, uint64_t Generation) const
{
    const auto Current = QueryThumbnail(Path);
    return Generation && Current.Ready && Current.Generation == Generation;
}
uint64_t MaterialLibraryRendering::QueryBakeCount() const noexcept { return Allocation ? Allocation->BakeCount : 0; }
VkImage MaterialLibraryRendering::QueryPreviewImage() const noexcept { return Allocation ? Allocation->Preview.QueryImage() : VK_NULL_HANDLE; }
VkImage MaterialLibraryRendering::QueryAtlasImage() const noexcept { return Allocation ? Allocation->Atlas.QueryImage() : VK_NULL_HANDLE; }
uint32_t MaterialLibraryRendering::QueryAtlasExtent() const noexcept { return Allocation ? Allocation->Atlas.QueryExtent() : 0; }
std::string MaterialLibraryRendering::QueryThumbnailDiagnostic(const std::filesystem::path &Path) const
{
    if (Allocation) for (const auto &Slot : Allocation->Slots) if (Slot.Path == Path) return Slot.Diagnostic;
    return {};
}
VkImageView MaterialLibraryRendering::QueryAtlasView() const noexcept { return Allocation ? Allocation->Atlas.QueryImageView() : VK_NULL_HANDLE; }
VkImageView MaterialLibraryRendering::QueryPreviewView() const noexcept { return Allocation ? Allocation->Preview.QueryImageView() : VK_NULL_HANDLE; }
bool MaterialLibraryRendering::QueryPreviewReady() const noexcept { return Allocation && Allocation->PreviewReady; }
const std::string &MaterialLibraryRendering::QueryDiagnostic() const noexcept
{
    static const std::string Empty;
    return Allocation ? Allocation->Diagnostic : Empty;
}
} // namespace Frontier

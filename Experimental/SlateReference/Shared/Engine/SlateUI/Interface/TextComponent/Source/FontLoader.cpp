#include "SlateUI/Interface/TextComponent/Api/FontLoader.h"

#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <cstring>
#include <cstdio>

namespace Slate
{
namespace
{
std::string Lower(std::string Text)
{
    for (char& Letter : Text)
        Letter = static_cast<char>(std::tolower(static_cast<unsigned char>(Letter)));
    return Text;
}

std::size_t Slot(FontWeight Weight, FontSlant Slant)
{
    const std::size_t Step = (static_cast<std::uint32_t>(Weight) - 100u) / 100u;
    return Step * 2u + static_cast<std::uint32_t>(Slant);
}

bool Matches(const std::string& Name, FontWeight Weight, FontSlant Slant)
{
    const std::string LowerName = Lower(Name);
    const char* Word = (Weight == FontWeight::Thin) ? "thin" :
                       (Weight == FontWeight::ExtraLight) ? "extralight" :
                       (Weight == FontWeight::Light) ? "light" :
                       (Weight == FontWeight::Medium) ? "medium" :
                       (Weight == FontWeight::Semibold) ? "semibold" :
                       (Weight == FontWeight::Bold) ? "bold" :
                       (Weight == FontWeight::ExtraBold) ? "extrabold" :
                       (Weight == FontWeight::Black) ? "black" : "regular";
    return LowerName.find(Word) != std::string::npos &&
           (Slant == FontSlant::Italic ? LowerName.find("italic") != std::string::npos
                                       : LowerName.find("italic") == std::string::npos);
}
}

Deliver<bool> FontLoader::Discover(const char* FontRoot)
{
    Root = FontRoot != nullptr ? FontRoot : "";
    Families.clear();
    PreviewFaces.clear();
    if (FontRoot == nullptr || !std::filesystem::exists(FontRoot))
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "font archive directory is unavailable" });

    for (const auto& Entry : std::filesystem::directory_iterator(FontRoot))
    {
        if (Entry.is_directory())
            Families.push_back(Entry.path().filename().string());
    }
    std::sort(Families.begin(), Families.end());
    std::fprintf(stderr, "[Fonts] discovered %u families in %s\n",
                 static_cast<unsigned>(Families.size()), FontRoot);
    return Deliver<bool>::Result(true);
}

const char* FontLoader::FamilyName(std::uint32_t Index) const
{
    return Index < Families.size() ? Families[Index].c_str() : nullptr;
}

Deliver<bool> FontLoader::PreparePreviews(float DisplayScale)
{
    if (Root.empty() || ImGui::GetCurrentContext() == nullptr || ImGui::GetIO().Fonts == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "font context is unavailable" });

    const std::size_t StandingCount = PreviewFaces.size();
    for (const std::string& Family : Families)
        static_cast<void>(Preview(Family.c_str(), DisplayScale));

    // 📝 Build only when a new preview face entered the atlas. The validation host used to call this on
    //    every resize and every theme change, and an unconditional Build re-packed every font in the
    //    atlas each time — a full CPU re-rasterisation for a call that added nothing.
    if (PreviewFaces.size() != StandingCount)
        ImGui::GetIO().Fonts->Build();
    std::fprintf(stderr, "[Fonts] prepared %u preview faces before recording\n",
                 static_cast<unsigned>(PreviewFaces.size()));
    return Deliver<bool>::Result(true);
}

bool FontLoader::HasFace(FontWeight Weight, FontSlant Slant) const
{
    return Faces[Slot(Weight, Slant)] != nullptr;
}

ImFont* FontLoader::Face(FontWeight Weight, FontSlant Slant) const
{
    ImFont* Loaded = Faces[Slot(Weight, Slant)];
    if (Loaded != nullptr)
        return Loaded;
    return Faces[Slot(FontWeight::Regular, FontSlant::Upright)];
}

ImFont* FontLoader::Preview(const char* Family, float DisplayScale)
{
    if (Family == nullptr || Root.empty() || ImGui::GetCurrentContext() == nullptr)
        return nullptr;
    for (const PreviewFace& Cached : PreviewFaces)
        if (Cached.Family == Family)
            return Cached.Face;
    if (ImGui::GetCurrentContext() == nullptr)
        return nullptr;
    const std::filesystem::path Directory = std::filesystem::path(Root) / Family;
    if (!std::filesystem::exists(Directory)) return nullptr;
    const float Size = 16.0f * ((DisplayScale > 0.0f) ? DisplayScale : 1.0f);
    for (const auto& Entry : std::filesystem::directory_iterator(Directory))
    {
        const std::string Name = Lower(Entry.path().filename().string());
        if (Entry.is_regular_file() && Name.find("regular") != std::string::npos && Name.find("italic") == std::string::npos)
        {
            ImFont* Loaded = ImGui::GetIO().Fonts->AddFontFromFileTTF(Entry.path().string().c_str(), Size);
            PreviewFaces.push_back({ Family, Loaded });
            return Loaded;
        }
    }
    for (const auto& Entry : std::filesystem::directory_iterator(Directory))
    {
        const std::string Name = Lower(Entry.path().filename().string());
        if (Entry.is_regular_file() && Name.find("italic") == std::string::npos &&
            (Name.ends_with(".ttf") || Name.ends_with(".otf")))
        {
            ImFont* Loaded = ImGui::GetIO().Fonts->AddFontFromFileTTF(Entry.path().string().c_str(), Size);
            PreviewFaces.push_back({ Family, Loaded });
            return Loaded;
        }
    }
    return nullptr;
}

Deliver<bool> FontLoader::Load(const char* FontRoot, const FontProfile& Profile, float DisplayScale)
{
    if (FontRoot == nullptr || ImGui::GetCurrentContext() == nullptr || ImGui::GetIO().Fonts == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "font context is unavailable" });

    const char* Family = (Profile.Family[0] != '\0') ? Profile.Family : "Inter";
    const std::filesystem::path FamilyRoot = std::filesystem::path(FontRoot) / Family;
    if (!std::filesystem::exists(FamilyRoot))
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "selected font family is not installed" });

    const float Size = 16.0f * ((DisplayScale > 0.0f) ? DisplayScale : 1.0f);

    // 🔴 Retire the previous family's faces before adding the next. Without this every family switch
    //    appended another set of faces to the same atlas, and every later Build re-packed the WHOLE
    //    accumulated set — the atlas grew without bound, and with it the CPU time and RAM of each
    //    switch. `RemoveFont` deletes the face and its baked output; the next Build repacks the rest.
    //    The atlas is unlocked between frames, which is the only window Load is called in (hosts flush
    //    pending loads at the top of the tick, before NewFrame).
    for (ImFont* Loaded : LoadedFaces)
        if (Loaded != nullptr)
            ImGui::GetIO().Fonts->RemoveFont(Loaded);
    LoadedFaces.clear();

    Faces.fill(nullptr);
    std::uint32_t LoadedCount = 0u;
    for (std::uint32_t Weight = 100u; Weight <= 900u; Weight += 100u)
    {
        // 📝 Upright faces only. Nothing in the interface requests an italic face, and `Face()` already
        //    falls back to the upright face for an italic request — loading italics would double the
        //    rasterisation work for glyphs nothing draws.
        const FontWeight FaceWeight = static_cast<FontWeight>(Weight);
        for (const auto& Entry : std::filesystem::directory_iterator(FamilyRoot))
        {
            if (!Entry.is_regular_file() || !Matches(Entry.path().filename().string(), FaceWeight, FontSlant::Upright))
                continue;
            ImFont* Loaded = ImGui::GetIO().Fonts->AddFontFromFileTTF(Entry.path().string().c_str(), Size);
            if (Loaded != nullptr)
            {
                Faces[Slot(FaceWeight, FontSlant::Upright)] = Loaded;
                LoadedFaces.push_back(Loaded);
                ++LoadedCount;
                break;
            }
        }
    }

    // Variable fonts often publish one upright file instead of a file named Regular.
    // Use that file as the regular face rather than falling back to ImGui.
    if (Face(FontWeight::Regular, FontSlant::Upright) == nullptr)
    {
        for (const auto& Entry : std::filesystem::directory_iterator(FamilyRoot))
        {
            const std::string Name = Lower(Entry.path().filename().string());
            if (!Entry.is_regular_file() || Name.find("italic") != std::string::npos)
                continue;
            if (Name.ends_with(".ttf") || Name.ends_with(".otf"))
            {
                ImFont* Loaded = ImGui::GetIO().Fonts->AddFontFromFileTTF(Entry.path().string().c_str(), Size);
                if (Loaded != nullptr)
                {
                    Faces[Slot(FontWeight::Regular, FontSlant::Upright)] = Loaded;
                    LoadedFaces.push_back(Loaded);
                    ++LoadedCount;
                    break;
                }
            }
        }
    }

    if (Face(FontWeight::Regular, FontSlant::Upright) == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "selected font family has no usable upright face" });

    ImGui::GetIO().Fonts->Build();
    std::fprintf(stderr, "[Fonts] loaded %u faces for %s\n",
                 static_cast<unsigned>(LoadedCount), Family);
    return Deliver<bool>::Result(true);
}

void FontLoader::RequestLoad(const char* FontRoot, const FontProfile& Profile, float DisplayScale)
{
    PendingRoot = FontRoot != nullptr ? FontRoot : "";
    PendingProfile = Profile;
    PendingScale = DisplayScale;
    Pending = true;
}

Deliver<bool> FontLoader::FlushPending()
{
    if (!Pending)
        return Deliver<bool>::Result(true);
    Pending = false;
    return Load(PendingRoot.c_str(), PendingProfile, PendingScale);
}

} // namespace Slate

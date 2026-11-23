#pragma once

#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"
#include "Foundation/DeliveryGuarantee.h"

#include <array>
#include <string>
#include <vector>

struct ImFont;

namespace Slate
{

/// Loads the selected typeface faces into the active ImGui atlas.
class FontLoader
{
public:
    Deliver<bool> Discover(const char* FontRoot);
    Deliver<bool> PreparePreviews(float DisplayScale);
    Deliver<bool> Load(const char* FontRoot, const FontProfile& Profile, float DisplayScale);
    void RequestLoad(const char* FontRoot, const FontProfile& Profile, float DisplayScale);
    Deliver<bool> FlushPending();
    std::uint32_t FamilyCount() const { return static_cast<std::uint32_t>(Families.size()); }
    const char* FamilyName(std::uint32_t Index) const;
    ImFont* Active() const { return Face(FontWeight::Regular, FontSlant::Upright); }
    ImFont* Face(FontWeight Weight, FontSlant Slant) const;
    bool HasFace(FontWeight Weight, FontSlant Slant) const;
    ImFont* Preview(const char* Family, float DisplayScale);

private:
    std::array<ImFont*, 18u> Faces{};
    struct PreviewFace
    {
        std::string Family;
        ImFont* Face = nullptr;
    };
    std::vector<std::string> Families;
    std::vector<PreviewFace> PreviewFaces;
    // 📝 Every face `Load` added to the atlas, retired at the next `Load`. Without the retirement a
    //    family switch appended another seventeen faces to the SAME atlas, and each later Build re-packed
    //    the whole accumulated set — the CPU/RAM cost grew with every visit to the Fonts page.
    std::vector<ImFont*> LoadedFaces;
    std::string Root;
    std::string PendingRoot;
    FontProfile PendingProfile = {};
    float PendingScale = 1.0f;
    bool Pending = false;
};

} // namespace Slate

/*==========================================================================================================================================
                                                    DYNAMICMATERIALRECIPE.CPP
==========================================================================================================================================*/
// 🧩 Implementation of MaterialLayerStack operations, structural cache hashing & deterministic text serialization

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/Dynamic/DynamicMaterialRecipe.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace Frontier::MaterialSpace::Dynamic
{

namespace
{

constexpr uint64_t FnvOffsetBasis = 14695981039346656037ULL;
constexpr uint64_t FnvPrime       = 1099511628211ULL;

void HashByte(uint64_t& HashAccum, uint8_t Byte) noexcept
{
    HashAccum ^= static_cast<uint64_t>(Byte);
    HashAccum *= FnvPrime;
}

void HashUint32(uint64_t& HashAccum, uint32_t Word) noexcept
{
    HashByte(HashAccum, static_cast<uint8_t>(Word & 0xFFu));
    HashByte(HashAccum, static_cast<uint8_t>((Word >> 8u) & 0xFFu));
    HashByte(HashAccum, static_cast<uint8_t>((Word >> 16u) & 0xFFu));
    HashByte(HashAccum, static_cast<uint8_t>((Word >> 24u) & 0xFFu));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 MATERIAL LAYER STACK
//------------------------------------------------------------------------------------------------------------------------

void MaterialLayerStack::AppendLayer(MaterialLayerEntry Entry)
{
    SanitizeSurfaceMaterial(Entry.FinishMaterial);
    SanitizeSurfaceMaterial(Entry.SecondaryFinish);
    Entry.LayerOpacity = std::clamp(Entry.LayerOpacity, 0.0f, 1.0f);
    Layers.push_back(std::move(Entry));
}

bool MaterialLayerStack::RemoveLayer(std::size_t Index)
{
    if (Index >= Layers.size() || Layers.size() <= 1u)
    {
        return false;
    }
    Layers.erase(Layers.begin() + static_cast<std::ptrdiff_t>(Index));
    return true;
}

bool MaterialLayerStack::ReorderLayer(std::size_t SourceIndex, std::size_t TargetIndex)
{
    if (SourceIndex >= Layers.size() || TargetIndex >= Layers.size() || SourceIndex == TargetIndex)
    {
        return false;
    }
    MaterialLayerEntry Moved = std::move(Layers[SourceIndex]);
    Layers.erase(Layers.begin() + static_cast<std::ptrdiff_t>(SourceIndex));
    Layers.insert(Layers.begin() + static_cast<std::ptrdiff_t>(TargetIndex), std::move(Moved));
    return true;
}

void MaterialLayerStack::ToggleLayerVisibility(std::size_t Index) noexcept
{
    if (Index < Layers.size())
    {
        Layers[Index].VisibilityEnabled = !Layers[Index].VisibilityEnabled;
    }
}

void MaterialLayerStack::ToggleLayerSolo(std::size_t Index) noexcept
{
    if (Index >= Layers.size())
    {
        return;
    }
    const bool EnableSolo = !Layers[Index].SoloEnabled;
    for (auto& Layer : Layers)
    {
        Layer.SoloEnabled = false;
    }
    Layers[Index].SoloEnabled = EnableSolo;
}

bool MaterialLayerStack::QueryLayerEffectiveVisibility(std::size_t Index) const noexcept
{
    if (Index >= Layers.size())
    {
        return false;
    }
    bool AnySoloActive = false;
    for (const auto& Layer : Layers)
    {
        if (Layer.SoloEnabled)
        {
            AnySoloActive = true;
            break;
        }
    }
    if (AnySoloActive)
    {
        return Layers[Index].SoloEnabled;
    }
    return Layers[Index].VisibilityEnabled;
}

uint32_t MaterialLayerStack::CountVisibleLayers() const noexcept
{
    uint32_t Count = 0u;
    for (std::size_t Index = 0u; Index < Layers.size(); ++Index)
    {
        if (QueryLayerEffectiveVisibility(Index))
        {
            ++Count;
        }
    }
    return Count;
}

uint64_t MaterialLayerStack::ComputeStructuralSignatureHash() const noexcept
{
    // Structural hash intentionally ignores numeric sliders so dragging sliders triggers ZERO shader recompiles!
    uint64_t Hash = FnvOffsetBasis;
    HashUint32(Hash, static_cast<uint32_t>(Layers.size()));
    for (std::size_t Index = 0u; Index < Layers.size(); ++Index)
    {
        if (!QueryLayerEffectiveVisibility(Index))
        {
            continue;
        }
        const auto& Entry = Layers[Index];
        HashByte(Hash, static_cast<uint8_t>(Entry.Category));
        HashByte(Hash, static_cast<uint8_t>(Entry.FieldPattern));
        HashByte(Hash, static_cast<uint8_t>(Entry.MaskMode));
        HashUint32(Hash, Entry.ChannelMask);
    }
    return Hash;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

void SynchronizeRecipeParameters(DynamicMaterialRecipe& TargetRecipe) noexcept
{
    SanitizeSurfaceMaterial(TargetRecipe.PrimaryFinish);
    SanitizeSurfaceMaterial(TargetRecipe.SecondaryFinish);

    uint32_t CombinedMask = 0u;
    uint32_t VisibleCount = 0u;
    for (std::size_t Index = 0u; Index < TargetRecipe.LayerStack.Layers.size(); ++Index)
    {
        if (TargetRecipe.LayerStack.QueryLayerEffectiveVisibility(Index))
        {
            CombinedMask |= TargetRecipe.LayerStack.Layers[Index].ChannelMask;
            ++VisibleCount;
        }
    }

    TargetRecipe.Parameters.PresetOrdinal     = static_cast<uint32_t>(TargetRecipe.PresetIdentity);
    TargetRecipe.Parameters.ActiveLayerCount  = VisibleCount;
    TargetRecipe.Parameters.ActiveChannelMask = (CombinedMask != 0u) ? CombinedMask : 0xFFu;
    SanitizeDynamicParameterBlock(TargetRecipe.Parameters);
}

std::string SerializeDynamicMaterialRecipe(const DynamicMaterialRecipe& Recipe)
{
    std::ostringstream Out;
    Out << "FRONTIER_DYNAMIC_MATERIAL_V1\n";
    Out << "id=" << Recipe.RecipeId << "\n";
    Out << "title=" << Recipe.DisplayTitle << "\n";
    Out << "category=" << Recipe.CategoryLabel << "\n";
    Out << "preset=" << static_cast<uint32_t>(Recipe.PresetIdentity) << "\n";
    Out << "transparent=" << (Recipe.TransparentSurface ? 1 : 0) << "\n";
    Out << "double_sided=" << (Recipe.DoubleSidedSurface ? 1 : 0) << "\n";
    Out << "speed=" << Recipe.Parameters.AnimationSpeed << "\n";
    Out << "progress=" << Recipe.Parameters.TransitionProgress << "\n";
    Out << "scale=" << Recipe.Parameters.DetailScale << "\n";
    Out << "relief=" << Recipe.Parameters.ReliefStrength << "\n";
    Out << "opacity=" << Recipe.Parameters.OpacityScale << "\n";
    Out << "rain=" << Recipe.Parameters.WeatherRainIntensity << "\n";
    Out << "snow=" << Recipe.Parameters.WeatherSnowDepth << "\n";
    Out << "ior=" << Recipe.Parameters.OpticalIor << "\n";
    Out << "thin_film_nm=" << Recipe.Parameters.ThinFilmNanometers << "\n";
    Out << "seam_glow=" << Recipe.Parameters.SeamGlowIntensity << "\n";
    Out << "driver=" << Recipe.Parameters.PrimaryDriverOrdinal << "\n";
    Out << "primary_rgb=" << Recipe.Parameters.PrimaryColorRgba[0] << ","
        << Recipe.Parameters.PrimaryColorRgba[1] << "," << Recipe.Parameters.PrimaryColorRgba[2] << "\n";
    Out << "secondary_rgb=" << Recipe.Parameters.SecondaryColorRgba[0] << ","
        << Recipe.Parameters.SecondaryColorRgba[1] << "," << Recipe.Parameters.SecondaryColorRgba[2] << "\n";
    Out << "tertiary_rgb=" << Recipe.Parameters.TertiaryColorRgba[0] << ","
        << Recipe.Parameters.TertiaryColorRgba[1] << "," << Recipe.Parameters.TertiaryColorRgba[2] << "\n";
    Out << "layer_count=" << Recipe.LayerStack.Layers.size() << "\n";

    for (std::size_t Index = 0u; Index < Recipe.LayerStack.Layers.size(); ++Index)
    {
        const auto& L = Recipe.LayerStack.Layers[Index];
        Out << "layer=" << static_cast<uint32_t>(L.Category) << "|"
            << static_cast<uint32_t>(L.FieldPattern) << "|"
            << static_cast<uint32_t>(L.MaskMode) << "|"
            << static_cast<uint32_t>(L.DriverMode) << "|"
            << (L.VisibilityEnabled ? 1 : 0) << "|"
            << L.LayerOpacity << "|"
            << L.ChannelMask << "|"
            << L.ScaleMultiplier << "|"
            << L.IntensityMultiplier << "|"
            << L.SeamGlowStrength << "|"
            << L.LayerTitle << "\n";
    }
    return Out.str();
}

bool DeserializeDynamicMaterialRecipe(
    std::string_view       SerializedText,
    DynamicMaterialRecipe& OutRecipe)
{
    std::istringstream Stream { std::string(SerializedText) };
    std::string HeaderLine;
    if (!std::getline(Stream, HeaderLine) || HeaderLine != "FRONTIER_DYNAMIC_MATERIAL_V1")
    {
        return false;
    }

    DynamicMaterialRecipe Parsed {};
    Parsed.LayerStack.Layers.clear();

    const auto ParseRgb = [](const std::string& Text, std::array<float, 4u>& Target) noexcept
    {
        float R = 0.0f, G = 0.0f, B = 0.0f;
        if (std::sscanf(Text.c_str(), "%f,%f,%f", &R, &G, &B) == 3)
        {
            Target[0] = R;
            Target[1] = G;
            Target[2] = B;
        }
    };

    std::string Line;
    while (std::getline(Stream, Line))
    {
        const std::size_t EqPos = Line.find('=');
        if (EqPos == std::string::npos)
        {
            continue;
        }
        const std::string Key = Line.substr(0u, EqPos);
        const std::string Val = Line.substr(EqPos + 1u);

        if (Key == "id")                Parsed.RecipeId = Val;
        else if (Key == "title")        Parsed.DisplayTitle = Val;
        else if (Key == "category")     Parsed.CategoryLabel = Val;
        else if (Key == "preset")       Parsed.PresetIdentity = static_cast<DynamicPresetIdentity>(std::strtoul(Val.c_str(), nullptr, 10));
        else if (Key == "transparent")  Parsed.TransparentSurface = (Val == "1");
        else if (Key == "double_sided") Parsed.DoubleSidedSurface = (Val == "1");
        else if (Key == "speed")        Parsed.Parameters.AnimationSpeed = std::strtof(Val.c_str(), nullptr);
        else if (Key == "progress")     Parsed.Parameters.TransitionProgress = std::strtof(Val.c_str(), nullptr);
        else if (Key == "scale")        Parsed.Parameters.DetailScale = std::strtof(Val.c_str(), nullptr);
        else if (Key == "relief")       Parsed.Parameters.ReliefStrength = std::strtof(Val.c_str(), nullptr);
        else if (Key == "opacity")      Parsed.Parameters.OpacityScale = std::strtof(Val.c_str(), nullptr);
        else if (Key == "rain")         Parsed.Parameters.WeatherRainIntensity = std::strtof(Val.c_str(), nullptr);
        else if (Key == "snow")         Parsed.Parameters.WeatherSnowDepth = std::strtof(Val.c_str(), nullptr);
        else if (Key == "ior")          Parsed.Parameters.OpticalIor = std::strtof(Val.c_str(), nullptr);
        else if (Key == "thin_film_nm") Parsed.Parameters.ThinFilmNanometers = std::strtof(Val.c_str(), nullptr);
        else if (Key == "seam_glow")    Parsed.Parameters.SeamGlowIntensity = std::strtof(Val.c_str(), nullptr);
        else if (Key == "driver")       Parsed.Parameters.PrimaryDriverOrdinal = static_cast<uint32_t>(std::strtoul(Val.c_str(), nullptr, 10));
        else if (Key == "primary_rgb")  ParseRgb(Val, Parsed.Parameters.PrimaryColorRgba);
        else if (Key == "secondary_rgb")ParseRgb(Val, Parsed.Parameters.SecondaryColorRgba);
        else if (Key == "tertiary_rgb") ParseRgb(Val, Parsed.Parameters.TertiaryColorRgba);
        else if (Key == "layer")
        {
            MaterialLayerEntry Entry {};
            uint32_t Cat = 0u, Pat = 0u, Msk = 0u, Drv = 0u, Vis = 1u;
            std::istringstream Parts(Val);
            std::string Field;
            int Column = 0;
            while (std::getline(Parts, Field, '|'))
            {
                switch (Column)
                {
                    case 0:  Cat = static_cast<uint32_t>(std::strtoul(Field.c_str(), nullptr, 10)); break;
                    case 1:  Pat = static_cast<uint32_t>(std::strtoul(Field.c_str(), nullptr, 10)); break;
                    case 2:  Msk = static_cast<uint32_t>(std::strtoul(Field.c_str(), nullptr, 10)); break;
                    case 3:  Drv = static_cast<uint32_t>(std::strtoul(Field.c_str(), nullptr, 10)); break;
                    case 4:  Vis = static_cast<uint32_t>(std::strtoul(Field.c_str(), nullptr, 10)); break;
                    case 5:  Entry.LayerOpacity        = std::strtof(Field.c_str(), nullptr); break;
                    case 6:  Entry.ChannelMask         = static_cast<uint32_t>(std::strtoul(Field.c_str(), nullptr, 10)); break;
                    case 7:  Entry.ScaleMultiplier     = std::strtof(Field.c_str(), nullptr); break;
                    case 8:  Entry.IntensityMultiplier = std::strtof(Field.c_str(), nullptr); break;
                    case 9:  Entry.SeamGlowStrength    = std::strtof(Field.c_str(), nullptr); break;
                    case 10: Entry.LayerTitle          = Field; break;
                    default: break;
                }
                ++Column;
            }
            Entry.Category          = static_cast<MaterialLayerCategory>(Cat);
            Entry.FieldPattern      = static_cast<ProceduralFieldPattern>(Pat);
            Entry.MaskMode          = static_cast<MaskModifierMode>(Msk);
            Entry.DriverMode        = static_cast<ParameterDriverMode>(Drv);
            Entry.VisibilityEnabled = (Vis != 0u);
            Parsed.LayerStack.AppendLayer(std::move(Entry));
        }
    }

    if (Parsed.LayerStack.Layers.empty())
    {
        return false;
    }

    SynchronizeRecipeParameters(Parsed);
    OutRecipe = std::move(Parsed);
    return true;
}

} // namespace Frontier::MaterialSpace::Dynamic

/*==========================================================================================================================================
                                                               LOG CATEGORIES
                                     ------------------------------------------------------------------
                         Category registration is idempotent by name; palette spectra resolve from the approved Log
                                                            Stream token values.
==========================================================================================================================================*/

// 🧩 Category registration is idempotent by name; palette spectra resolve from the approved Log Stream token values.

#include "LogCategories.h"

#include <algorithm>
#include <cctype>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

LogColourSpectrum ResolveLogColour(LogColour Colour) noexcept
{
    // 📝 Spectra are the Log Stream HTML tokens verbatim (#2fe6a7, #63a8ff, #b18cff, #ff7ad9, #ffb454, muted #97a29b),
    //    so the native panel and the approved prototype cannot drift apart.
    switch (Colour)
    {
        case LogColour::Mint:   return {0.184f, 0.902f, 0.655f, 1.0f};
        case LogColour::Blue:   return {0.388f, 0.659f, 1.000f, 1.0f};
        case LogColour::Purple: return {0.694f, 0.549f, 1.000f, 1.0f};
        case LogColour::Pink:   return {1.000f, 0.478f, 0.851f, 1.0f};
        case LogColour::Amber:  return {1.000f, 0.706f, 0.329f, 1.0f};
        case LogColour::Neutral: break;
    }
    return {0.592f, 0.635f, 0.608f, 1.0f};
}

std::optional<LogColour> ParseLogColour(std::string_view Text) noexcept
{
    std::string Lower(Text);
    std::transform(Lower.begin(), Lower.end(), Lower.begin(),
                   [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });

    if (Lower == "mint")    return LogColour::Mint;
    if (Lower == "blue")    return LogColour::Blue;
    if (Lower == "purple")  return LogColour::Purple;
    if (Lower == "pink")    return LogColour::Pink;
    if (Lower == "amber")   return LogColour::Amber;
    if (Lower == "neutral") return LogColour::Neutral;
    return std::nullopt;
}

const char* LogColourText(LogColour Colour) noexcept
{
    switch (Colour)
    {
        case LogColour::Mint:   return "mint";
        case LogColour::Blue:   return "blue";
        case LogColour::Purple: return "purple";
        case LogColour::Pink:   return "pink";
        case LogColour::Amber:  return "amber";
        case LogColour::Neutral: break;
    }
    return "neutral";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       REGISTRY
//------------------------------------------------------------------------------------------------------------------------

LogCategoryRegistry::LogCategoryRegistry()
{
    // 📝 The nine engine channels, in the demo's own domain order. Fixed here so tokens are stable across runs.
    static const char* const EngineChannels[] = {
        "Render", "Illumination", "Physics", "Vehicle", "Tyre",
        "Environment", "Geometry", "Interface", "Content"
    };
    for (const char* Name : EngineChannels)
    {
        (void)Register(Name, LogColour::Neutral, false);   // [idx] - Tokens land 1..9 by construction
    }
}

uint32_t LogCategoryRegistry::Register(std::string_view Name, LogColour Colour, bool UserCreated)
{
    if (Name.empty())
    {
        return 0;
    }

    std::unique_lock<std::shared_mutex> Lock(Mutex);
    const auto                          Existing = ByName.find(std::string(Name));
    if (Existing != ByName.end())
    {
        // 📝 Idempotent by name; re-registering a user category updates its colour (engine channels stay fixed).
        LogCategory& Known = Categories[Existing->second - 1];
        if (Known.UserCreated)
        {
            Known.Colour = Colour;
        }
        return Existing->second;
    }

    LogCategory Category;
    Category.Token       = static_cast<uint32_t>(Categories.size()) + 1;
    Category.Name        = std::string(Name);
    Category.Colour      = Colour;
    Category.UserCreated = UserCreated;
    Categories.push_back(Category);
    ByName.emplace(Category.Name, Category.Token);
    return Category.Token;
}

const LogCategory* LogCategoryRegistry::Query(uint32_t Token) const
{
    std::shared_lock<std::shared_mutex> Lock(Mutex);
    if (Token == 0 || Token > Categories.size())
    {
        return nullptr;
    }
    return &Categories[Token - 1];
}

const LogCategory* LogCategoryRegistry::QueryByName(std::string_view Name) const
{
    std::shared_lock<std::shared_mutex> Lock(Mutex);
    const auto                          Found = ByName.find(std::string(Name));
    if (Found == ByName.end())
    {
        return nullptr;
    }
    return &Categories[Found->second - 1];
}

uint32_t LogCategoryRegistry::QueryCount() const
{
    std::shared_lock<std::shared_mutex> Lock(Mutex);
    return static_cast<uint32_t>(Categories.size());
}

void LogCategoryRegistry::QueryAll(std::vector<const LogCategory*>& Out) const
{
    std::shared_lock<std::shared_mutex> Lock(Mutex);
    Out.clear();
    Out.reserve(Categories.size());
    for (const LogCategory& Category : Categories)
    {
        Out.push_back(&Category);
    }
}

LogCategoryRegistry& LogCategories()
{
    static LogCategoryRegistry Registry;
    return Registry;
}

LogCategory RegisterLogCategory(std::string_view Name, LogColour Colour)
{
    LogCategoryRegistry& Registry = LogCategories();
    const uint32_t       Token    = Registry.Register(Name, Colour, true);
    if (const LogCategory* Category = Registry.Query(Token))
    {
        return *Category;
    }
    return LogCategory{};
}

} // namespace Frontier

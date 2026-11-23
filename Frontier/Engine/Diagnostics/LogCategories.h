/*==========================================================================================================================================
                                                           LOG CATEGORY REGISTRY
                                     ------------------------------------------------------------------
                         Engine channels are pre-registered, user code registers its own categories with a palette
                                                                  colour.
==========================================================================================================================================*/

// 🧩 Log category registry: engine channels are pre-registered, user code registers its own categories with a palette colour.

#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         TYPES
//------------------------------------------------------------------------------------------------------------------------

// Palette keys shared with Appearance tokens; Neutral renders in the muted engine tone.
enum class LogColour : uint8_t
{
    Neutral,
    Mint,
    Blue,
    Purple,
    Pink,
    Amber
};

struct LogColourSpectrum
{
    float Red   = 1.0f;   // [0-1] - Linear-appearance red component
    float Green = 1.0f;   // [0-1] - Green component
    float Blue  = 1.0f;   // [0-1] - Blue component
    float Alpha = 1.0f;   // [0-1] - Opacity
};

[[nodiscard]] LogColourSpectrum          ResolveLogColour(LogColour Colour) noexcept;
[[nodiscard]] std::optional<LogColour>   ParseLogColour(std::string_view Text) noexcept;   // Palette token text ("pink", "purple", ...)
[[nodiscard]] const char*                LogColourText(LogColour Colour) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct LogCategory
{
    uint32_t    Token       = 0;                 // [idx] - Registry token stored in every LogEntry; 0 is invalid
    std::string Name;                            // [-]   - Display name ("Render", "Foliage")
    LogColour   Colour      = LogColour::Neutral; // [-]  - Palette colour for tags and menu dots
    bool        UserCreated = false;             // [-]   - True for categories registered by user/project code
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       REGISTRY
//------------------------------------------------------------------------------------------------------------------------

// Registration is idempotent by name: re-registering an existing name returns its original token and keeps
// the original colour, so independent translation units can declare the same category safely.
class LogCategoryRegistry
{
  public:
    LogCategoryRegistry();   // Pre-registers the nine engine channels in a fixed order (tokens 1-9)

    LogCategoryRegistry(const LogCategoryRegistry&)            = delete;
    LogCategoryRegistry& operator=(const LogCategoryRegistry&) = delete;

    [[nodiscard]] uint32_t Register(std::string_view Name, LogColour Colour, bool UserCreated);

    // Returned pointers stay valid for the registry lifetime (deque storage); they are never invalidated by growth.
    [[nodiscard]] const LogCategory* Query(uint32_t Token) const;
    [[nodiscard]] const LogCategory* QueryByName(std::string_view Name) const;
    [[nodiscard]] uint32_t           QueryCount() const;

    // Snapshot of every registered category in token order (panel menus).
    void QueryAll(std::vector<const LogCategory*>& Out) const;

  private:
    mutable std::shared_mutex                    Mutex;
    std::deque<LogCategory>                      Categories;   // [entries] - Index 0 holds token 1
    std::unordered_map<std::string, uint32_t>    ByName;       // [idx] - Name -> token
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] LogCategoryRegistry& LogCategories();   // Process-wide registry

// User-facing registration. Declare once (for example at namespace scope) and keep the returned category:
//   static const LogCategory FoliageCategory = RegisterLogCategory("Foliage", LogColour::Pink);
[[nodiscard]] LogCategory RegisterLogCategory(std::string_view Name, LogColour Colour);

} // namespace Frontier

/*==========================================================================================================================================
                                                          STACK RAIL CONTROLS
                                     ------------------------------------------------------------------
            First kit set beyond the Log Stream: StackRail chrome, TreeRow, IdentityHeader (interface SDK builds).
==========================================================================================================================================*/

// 🧩 First kit set beyond the Log Stream: StackRail chrome, TreeRow, IdentityHeader (interface SDK builds only).

#pragma once

// 📝 Plan §10.4 order of construction: StackRail follows the Log Stream's set because the SceneDirectory is its
//    first consumer; TreeRow carries the demo's outliner row vocabulary (§10.2); IdentityHeader tops inspectors.
//    The vocabulary is Texture Workspace's (Rail + rail-head + HeadCount + AddHost + Filter + stack-sled +
//    StackFoot + Readout; inspectIcon/inspectName/inspectSub), the drawing idiom is the Log Stream's proven one
//    (window draw list + invisible hit targets + DM Sans through the upstream typeface registry), and menus stay
//    on the reusable upstream DropdownControlSurface. Declarations exist only with the interface SDK present.
#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include <imgui.h>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         TOKENS
//------------------------------------------------------------------------------------------------------------------------

constexpr ImU32 KitInkRgba(int Red, int Green, int Blue, int Alpha) noexcept
{
    return static_cast<ImU32>(Alpha) << 24 | static_cast<ImU32>(Blue) << 16 | static_cast<ImU32>(Green) << 8 |
           static_cast<ImU32>(Red);
}

constexpr ImU32 KitInkHex(int Rgb, int Alpha = 255) noexcept
{
    return KitInkRgba((Rgb >> 16) & 255, (Rgb >> 8) & 255, Rgb & 255, Alpha);
}

constexpr ImU32 KitInkWhite(float Alpha) noexcept
{
    return KitInkRgba(255, 255, 255, static_cast<int>(Alpha * 255.0f + 0.5f));
}

constexpr ImU32 KitInkWithAlpha(ImU32 Colour, float Alpha) noexcept
{
    return (Colour & 0x00ffffffu) | static_cast<ImU32>(static_cast<int>(Alpha * 255.0f + 0.5f)) << 24;
}

// Same values as the Log Stream's panel theme (one visual language; consolidation follows when a third
// consumer arrives — duplication of constants is deliberate until then).
struct KitTheme
{
    static constexpr ImU32 PanelBg   = KitInkHex(0x0b100e);
    static constexpr ImU32 PanelEdge = KitInkWhite(0.08f);
    static constexpr ImU32 Surface   = KitInkHex(0x0e1411);
    static constexpr ImU32 Surface2  = KitInkHex(0x151d18);
    static constexpr ImU32 Surface3  = KitInkHex(0x1c2620);
    static constexpr ImU32 Line      = KitInkWhite(0.07f);
    static constexpr ImU32 LineSoft  = KitInkWhite(0.045f);
    static constexpr ImU32 RowHover  = KitInkWhite(0.032f);
    static constexpr ImU32 Text      = KitInkHex(0xeef3f0);
    static constexpr ImU32 Muted     = KitInkHex(0x97a29b);
    static constexpr ImU32 Faint     = KitInkHex(0x5f6b64);
    static constexpr ImU32 Mint      = KitInkHex(0x2fe6a7);
    static constexpr ImU32 MintDim   = KitInkRgba(47, 230, 167, 33);
    static constexpr ImU32 ChipOnBg  = KitInkHex(0xe9f2ed);
    static constexpr ImU32 ChipOnInk = KitInkHex(0x0b120e);
};

struct KitFaces
{
    ImFont* Light    = nullptr;
    ImFont* Regular  = nullptr;
    ImFont* Medium   = nullptr;
    ImFont* SemiBold = nullptr;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] KitFaces ResolveKitFaces() noexcept;
[[nodiscard]] float    KitTextWidth(ImFont* Font, float Size, const char* Begin, const char* End = nullptr) noexcept;
void                   KitDrawText(ImDrawList* Ink, ImFont* Font, float Size, ImVec2 Position, ImU32 Colour,
                                   const char* Begin, const char* End = nullptr);
float                  KitDrawTracked(ImDrawList* Ink, ImFont* Font, float Size, ImVec2 Position, ImU32 Colour,
                                      const char* Text, float Tracking);
[[nodiscard]] bool     KitTarget(const char* Id, ImVec2 Minimum, ImVec2 Size, bool& Hovered,
                                 const char* Tooltip = nullptr);
void                   KitPillSurface(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, ImU32 Background,
                                      ImU32 Border, float Radius);

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONTROLS
//------------------------------------------------------------------------------------------------------------------------

// StackRail head: title, live count pill, add button at the right. Returns true when Add was clicked.
[[nodiscard]] bool DrawStackRailHead(const KitFaces& Faces, ImVec2 Minimum, float Width, const char* Title,
                                     const char* CountText);

// StackRail foot: left/right readouts on one line, hairline above.
void DrawStackRailFoot(const KitFaces& Faces, ImVec2 Minimum, float Width, const char* LeftText,
                       const char* RightText);

// IdentityHeader: icon disc with classification colour, name, sub-line (inspector tops).
void DrawIdentityHeader(const KitFaces& Faces, ImVec2 Minimum, float Width, ImU32 IconColour, const char* Name,
                        const char* Sub);

struct TreeRowInteraction
{
    bool Clicked        = false;   // Row body (selection)
    bool ChevronClicked = false;   // Fold toggle (only meaningful with children)
    bool EyeClicked     = false;   // Visibility toggle
};

// One Outliner tree row (demo .tree-row: name, meta, visibility, selected/off states), 30 px tall.
[[nodiscard]] TreeRowInteraction DrawTreeRow(const KitFaces& Faces, const char* Id, ImVec2 Minimum, float Width,
                                             float Indent, bool HasChildren, bool Collapsed, ImU32 DotColour,
                                             const char* Name, const char* Meta, bool Visible, bool Selected);

} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK

#pragma once
#include "IconArt.h"
#include <imgui.h>

namespace Frontier {
// Context-owned ImGui adapter. Attach after context creation, before the first frame.
// Requires RendererHasTextures and backend shutdown BEFORE ImGui::DestroyContext().
// Rasterization happens at NewFramePre, never inside Draw(). Single UI thread only.
class IconPresentation final {
public:
    static void Attach(const std::filesystem::path& Root = "EngineContent/Icons");
    // False means no attached/texture-capable adapter (caller may use its legacy glyph).
    // Unsupported artwork still returns true: the strict, visible substitute is drawn.
    static bool Draw(ImDrawList* List, IconSymbol Symbol, ImVec2 Min, float Size,
                     float Opacity = 1.0f, IconDomain Domain = IconDomain::Editor, ImU32 Tint = IM_COL32_WHITE);
    static IconResult Result(IconSymbol Symbol);
    static const char* Diagnostic(IconSymbol Symbol);
};
}

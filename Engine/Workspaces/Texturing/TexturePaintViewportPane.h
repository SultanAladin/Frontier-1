/*==========================================================================================================================================
                                                     TEXTUREPAINTVIEWPORTPANE.H
==========================================================================================================================================*/
// 🖼️ Center Texture Paint Viewport: 2-band CardDock header, real 3D mesh & 2D UV raster display, 3D gradient gizmos & Brush Studio modal.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Texturing/TexturePaintSession.h"

#include <cstdint>
#include <functional>
#include <imgui.h>
#include <vector>

namespace Frontier::Workspaces
{

class TexturePaintViewportPane
{
public:
    TexturePaintViewportPane() noexcept = default;
    ~TexturePaintViewportPane() noexcept;

    TexturePaintViewportPane(const TexturePaintViewportPane&)            = delete;
    TexturePaintViewportPane& operator=(const TexturePaintViewportPane&) = delete;

    void AttachSession(TexturePaintSession* TargetSession) noexcept;
    void SetBrushStudioRequestCallback(std::function<void(float, float)> Callback) noexcept { BrushStudioRequestCallback = std::move(Callback); }
    void PresentInImGuiPane(double DeltaSeconds, float ScaleFactor);

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  DeltaSeconds);

    void ConfigureBrushInspectorOpen(bool Open) noexcept { BrushStudioModalOpen = Open; }
    [[nodiscard]] bool QueryBrushInspectorOpen() const noexcept { return BrushStudioModalOpen; }

    void ConfigureMoreToolsSheetOpen(bool Open) noexcept { MoreToolsSheetOpen = Open; }
    [[nodiscard]] bool QueryMoreToolsSheetOpen() const noexcept { return MoreToolsSheetOpen; }

private:
    TexturePaintSession*              Session                    = nullptr;
    std::function<void(float, float)> BrushStudioRequestCallback {};
    bool                              BrushStudioModalOpen       = false;
    bool                 ResolutionSheetOpen   = false;
    bool                 ViewModeSheetOpen     = false;
    bool                 MoreToolsSheetOpen    = false;
    bool                 SaveSheetOpen         = false;
    float                ModalScrollY          = 0.0f;
    float                ModalMaxScrollY       = 0.0f;
    TextEditBuffer       DocNameBuffer         {};
    TextEditBuffer       BrushSearchBuffer     {};
    TextEditBuffer       StampTextBuffer       {};

    // Cached 3D & 2D viewport textures for ImGui presentation
    ImTextureData        SpatialTexture        {};
    uint32_t             SpatialTexWidth       = 0u;
    uint32_t             SpatialTexHeight      = 0u;
    uint64_t             SpatialTexRevision    = 0u;
    float                CachedYaw             = 0.0f;
    float                CachedPitch           = 0.0f;
    float                CachedDistance        = 0.0f;
    uint32_t             CachedViewChannel     = 999u;
    bool                 CachedWireframe       = false;
    std::vector<uint8_t> SpatialRgbaBuffer     {};

    ImTextureData        UvCanvasTexture       {};
    uint32_t             UvTexWidth            = 0u;
    uint32_t             UvTexHeight           = 0u;
    uint64_t             UvTexRevision         = 0u;
    std::vector<uint8_t> UvRgbaBuffer          {};

    Rectangle2D          LastSpatialCanvasRect {};
    Rectangle2D          LastUvCanvasRect      {};
};

} // namespace Frontier::Workspaces

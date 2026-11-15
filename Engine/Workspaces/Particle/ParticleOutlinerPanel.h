/*============================================================================================================================================
                                                           PARTICLEOUTLINERPANEL.H
============================================================================================================================================*/
// 🧩 Floating 22px charcoal Particle Outliner panel with Visible/Hidden census tiles, 18 filter chips & Environment/Particle hierarchy

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Particle/ParticleWorkspaceSession.h"

namespace Frontier::Workspaces
{

class ParticleOutlinerPanel
{
public:
    static constexpr float RecommendedWidthPx = 306.0f; // [px] - Canonical Fluid/Project Zero outliner width

    void Render(
        PixelSpace&               Surface,
        Rectangle2D               Bounds,
        ParticleWorkspaceSession& Session,
        const ControlPointer&     Pointer,
        float                     Opacity = 1.0f) noexcept;

    [[nodiscard]] Rectangle2D QueryLastForceRowBounds(std::size_t ForceIndex) const noexcept;
    [[nodiscard]] Rectangle2D QueryLastRigRowBounds(std::size_t RigIndex) const noexcept;

private:
    std::vector<Rectangle2D> LastForceRowRects {}; // [px] - Cached hitboxes for validation & interaction
    std::vector<Rectangle2D> LastRigRowRects   {}; // [px] - Cached hitboxes for validation & interaction
};

} // namespace Frontier::Workspaces

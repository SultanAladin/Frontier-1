/*============================================================================================================================================
                                                        PARTICLEINSPECTORPANEL.H
============================================================================================================================================*/
// 🧩 Floating 22px Sun+Fluid Particle Inspector panel (Overview, Source + ScalarCurveEditor, Motion & FX + ColourRampEditor, Metrics & Force Fields)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Particle/ParticleWorkspaceSession.h"

#include <array>

namespace Frontier::Workspaces
{

class ParticleInspectorPanel
{
public:
    static constexpr float RecommendedWidthPx = 368.0f; // [px] - Canonical Sun Inspector right-panel width

    void Render(
        PixelSpace&               Surface,
        Rectangle2D               Bounds,
        ParticleWorkspaceSession& Session,
        const ControlPointer&     Pointer,
        ContentBrowserPanel*      AttachedBrowser = nullptr,
        float                     Opacity         = 1.0f) noexcept;

    [[nodiscard]] Rectangle2D QueryTabBounds(ParticleInspectorTab Tab) const noexcept;
    [[nodiscard]] Rectangle2D QueryColourRampStripBounds(const ParticleWorkspaceSession& Session) const noexcept
    {
        return Session.RampEditorSession.LastStripBounds;
    }

private:
    void RenderOverviewTab(PixelSpace& Surface, Rectangle2D Area, AuthoredParticleRigUi& Rig, ParticleWorkspaceSession& Session, const ControlPointer& Pointer, float Opacity) noexcept;
    void RenderSourceTab(PixelSpace& Surface, Rectangle2D Area, AuthoredParticleRigUi& Rig, ParticleWorkspaceSession& Session, const ControlPointer& Pointer, float Opacity) noexcept;
    void RenderMotionFxTab(PixelSpace& Surface, Rectangle2D Area, AuthoredParticleRigUi& Rig, ParticleWorkspaceSession& Session, const ControlPointer& Pointer, ContentBrowserPanel* AttachedBrowser, float Opacity) noexcept;
    void RenderMetricsTab(PixelSpace& Surface, Rectangle2D Area, AuthoredParticleRigUi& Rig, ParticleWorkspaceSession& Session, const ControlPointer& Pointer, float Opacity) noexcept;
    void RenderForceFieldInspector(PixelSpace& Surface, Rectangle2D Area, ParticleWorkspaceSession& Session, const ControlPointer& Pointer, float Opacity) noexcept;

    std::array<Rectangle2D, 4> LastTabRects     {};
    std::array<bool, 10>       SliderDragActive {};
};

} // namespace Frontier::Workspaces

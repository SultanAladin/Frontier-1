/*============================================================================================================================================
                                                  DETERMINISTICPATHTRACERPANE.H
============================================================================================================================================*/
// 🧩 Dark studio viewport + 330 px inspector pane for Order 3 Unified Deterministic Path Tracer (GameRealtime & SceneFinalRender)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/GeometricRaster/GlobalIllumination/DeterministicPathTraceSequence.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct ImTextureData;

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                             DETERMINISTIC PATH TRACER PANE
//------------------------------------------------------------------------------------------------------------------------

class DeterministicPathTracerPane
{
public:
    static constexpr float InspectorWidthPx = 330.0f;

    DeterministicPathTracerPane();
    ~DeterministicPathTracerPane();

    DeterministicPathTracerPane(const DeterministicPathTracerPane&)            = delete;
    DeterministicPathTracerPane& operator=(const DeterministicPathTracerPane&) = delete;

    void AttachSequence(GeometricRaster::DeterministicPathTraceSequence* ActiveSequence) noexcept;

    [[nodiscard]] GeometricRaster::DeterministicPathTraceSequence* AccessSequence() noexcept
    {
        return TracerSequence;
    }

    // Advances progressive or real-time sample integration when unpaused and resolves the display texture
    void StepIntegrationTick(Rectangle2D Bounds, double DeltaSeconds, uint32_t SamplesOverride = 0u);

    // Renders the viewport texture, overlay sample/FPS selectors, 2 px #c7f03a convergence bar, and 330 px dark studio inspector
    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  DeltaSeconds);

    void TriggerBitExactRepeatVerification(uint32_t SampleTotal = 8u, uint32_t AlternateBatch = 4u);

    [[nodiscard]] bool               QueryPaused() const noexcept          { return IntegrationPaused; }
    void                             AssignPaused(bool Paused) noexcept    { IntegrationPaused = Paused; }
    void                             ConfigureOpenDropdownId(int32_t Id) noexcept { OpenDropdownId = Id; }
    [[nodiscard]] const std::string& QueryDigestText() const noexcept      { return LastDigestText; }
    [[nodiscard]] const std::string& QueryRepeatVerdict() const noexcept   { return RepeatVerdictText; }

private:
    void SyncViewportTexture();

    GeometricRaster::DeterministicPathTraceSequence* TracerSequence    = nullptr;
    std::unique_ptr<ImTextureData>                   ViewportTexture   {};
    std::vector<uint8_t>                             DisplayPixels     {};
    uint32_t                                         TextureWidthPx    = 0u;
    uint32_t                                         TextureHeightPx   = 0u;
    bool                                             IntegrationPaused = false;
    bool                                             OrbitDragging     = false;
    Point2D                                          PreviousPointer   {};
    int32_t                                          ActiveSliderId    = -1;
    int32_t                                          OpenDropdownId    = -1;
    float                                            InspectorScrollY  = 0.0f;
    double                                           SmoothedRate      = 0.0;
    std::string                                      LastDigestText    { "—" };
    std::string                                      RepeatVerdictText {};
    bool                                             RepeatPassed      = true;
};

} // namespace Frontier

/*============================================================================================================================================
                                                            CAMERAINSPECTOR.H
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Editor / Cine Camera Inspector (4 tabs: Overview, Focus, Body, Render)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

#include <string_view>

namespace Frontier
{

class CameraInspector
{
public:
    CameraInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Permanent editor navigation camera \xC2\xB7 not a scene render camera"; }

    // Attaches a camera component (env:lens-flare, env:motion-blur, env:exposure, env:post-process, env:camera-settings)
    // as a child layer directly on CameraToken in Outliner and initializes its instance data.
    static OutlinerSlotToken AttachCameraComponent(
        OutlinerPanel&                                  Outliner,
        InspectorInstanceRegister&                      Instances,
        const GeometricRaster::WorldSceneSpecification& Scene,
        OutlinerSlotToken                               CameraToken,
        std::string_view                                ComponentTag);

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, CameraInstanceData& Instance,
        OutlinerPanel& Outliner, InspectorInstanceRegister& Instances,
        const GeometricRaster::WorldSceneSpecification& Scene,
        ViewportDisplaySettings* DisplaySettings,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = (PageIndex < 4u) ? PageIndex : 3u; }
    [[nodiscard]] uint32_t QueryPreviewUploads() const noexcept { return BodyPreview.QueryUploadCount(); }
    [[nodiscard]] uint32_t QueryMountUploads()   const noexcept { return BodyPreview.QueryUploadCount(); }

private:
    void UpdateCameraBodySurface(uint32_t RigPresetIndex);

    uint32_t                ActivePage             = 0u;
    uint32_t                ActiveDragId           = 0u;
    bool                    AddComponentDrawerOpen = false;
    InspectorPreviewSurface BodyPreview            {};
};

} // namespace Frontier

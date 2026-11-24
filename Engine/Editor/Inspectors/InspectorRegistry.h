/*============================================================================================================================================
                                                           INSPECTORREGISTRY.H
============================================================================================================================================*/
// 🧩 Generic Editor Inspector registry dispatching any Outliner entry to its standalone Inspector with dynamic breadcrumbs & multi-instance sync

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/InspectorPanel.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/Panes/ViewportPanel.h"
#include "Engine/Editor/Inspectors/AnalyticPrimitiveInspector.h"
#include "Engine/Editor/Inspectors/AreaLightInspector.h"
#include "Engine/Editor/Inspectors/AtmosphereInspector.h"
#include "Engine/Editor/Inspectors/AtmosphericFogInspector.h"
#include "Engine/Editor/Inspectors/CameraInspector.h"
#include "Engine/Editor/Inspectors/CameraSettingsInspector.h"
#include "Engine/Editor/Inspectors/CloudInspector.h"
#include "Engine/Editor/Inspectors/EnvironmentLightInspector.h"
#include "Engine/Editor/Inspectors/FolderInspector.h"
#include "Engine/Editor/Inspectors/GodRaysInspector.h"
#include "Engine/Editor/Inspectors/HeightFogInspector.h"
#include "Engine/Editor/Inspectors/IesLightInspector.h"
#include "Engine/Editor/Inspectors/ImportedGeometryInspector.h"
#include "Engine/Components/Controls/SmoothScrollView.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"
#include "Engine/Editor/Inspectors/LedLightInspector.h"
#include "Engine/Editor/Inspectors/LedStripInspector.h"
#include "Engine/Editor/Inspectors/LensFlareInspector.h"
#include "Engine/Editor/Inspectors/LocalCloudInspector.h"
#include "Engine/Editor/Inspectors/LocalFogInspector.h"
#include "Engine/Editor/Inspectors/LunarInspector.h"
#include "Engine/Editor/Inspectors/MotionBlurInspector.h"
#include "Engine/Editor/Inspectors/NativeGeometryInspector.h"
#include "Engine/Editor/Inspectors/PointLightInspector.h"
#include "Engine/Editor/Inspectors/PostProcessInspector.h"
#include "Engine/Editor/Inspectors/ExposureInspector.h"
#include "Engine/Editor/Inspectors/PrecipitationInspector.h"
#include "Engine/Editor/Inspectors/ProceduralGeometryInspector.h"
#include "Engine/Editor/Inspectors/ProjectInspectorFoundation.h"
#include "Engine/Editor/Inspectors/RainbowInspector.h"
#include "Engine/Editor/Inspectors/SolarInspector.h"
#include "Engine/Editor/Inspectors/SpotLightInspector.h"
#include "Engine/Editor/Inspectors/StarfieldInspector.h"
#include "Engine/Editor/Inspectors/TubeLightInspector.h"
#include "Engine/Editor/Inspectors/WindInspector.h"
#include "Engine/Editor/WorldEnvironmentRegistry.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"

#include <chrono>
#include <cstdint>

namespace Frontier
{

class InspectorRegistry
{
public:
    InspectorRegistry() noexcept = default;

    // Dispatches the currently selected Outliner entry to its dedicated standalone Inspector.
    [[nodiscard]] bool ConstructActiveInspector(
        PixelSpace&                               Surface,
        const Rectangle2D&                        PaneBounds,
        OutlinerPanel&                            Outliner,
        WorldEnvironmentRegistry*                 EnvironmentRegistry,
        GeometricRaster::WorldSceneSpecification& Scene,
        ViewportDisplaySettings*                  DisplaySettings,
        const PaneInputReading&                   Input,
        bool&                                     OutMutated) noexcept;

    void SetScrollOffset(float ScrollPixels) noexcept
    {
        ScrollView.ConfigureImmediateOffset(ScrollPixels);
        ++ActivePanelRevision;
    }
    [[nodiscard]] bool QueryScrollAnimating() const noexcept { return ScrollView.QueryAnimating(); }
    void ClearInstances() noexcept { Instances.Clear(); ProjectHost.ClearAll(); ++ActivePanelRevision; }
    [[nodiscard]] uint64_t QueryActiveSignature() const noexcept;
    [[nodiscard]] uint64_t QueryConstructCallCount() const noexcept { return ConstructCallCount; }
    [[nodiscard]] uint32_t QueryLastEvaluatedCardCount() const noexcept { return LastEvaluatedCardCount; }
    [[nodiscard]] uint32_t QueryLastSkippedCardCount() const noexcept { return LastSkippedCardCount; }

    [[nodiscard]] ProjectInspectorHost&      QueryProjectHost() noexcept { return ProjectHost; }
    [[nodiscard]] InspectorInstanceRegister& QueryInstances() noexcept { return Instances; }
    [[nodiscard]] AtmosphereInspector&       QueryAtmosphere() noexcept { return Atmosphere; }
    [[nodiscard]] SolarInspector&            QuerySolar() noexcept { return Solar; }
    [[nodiscard]] CloudInspector&            QueryCloud() noexcept { return Cloud; }
    [[nodiscard]] LocalCloudInspector&       QueryLocalCloud() noexcept { return LocalCloud; }
    [[nodiscard]] LunarInspector&            QueryLunar() noexcept { return Lunar; }
    [[nodiscard]] StarfieldInspector&        QueryStarfield() noexcept { return Starfield; }
    [[nodiscard]] WindInspector&             QueryWind() noexcept { return Wind; }
    [[nodiscard]] HeightFogInspector&        QueryHeightFog() noexcept { return HeightFog; }
    [[nodiscard]] AtmosphericFogInspector&   QueryAtmosphericFog() noexcept { return AtmosphericFog; }
    [[nodiscard]] LocalFogInspector&         QueryLocalFog() noexcept { return LocalFog; }
    [[nodiscard]] GodRaysInspector&          QueryGodRays() noexcept { return GodRays; }
    [[nodiscard]] PrecipitationInspector&    QueryPrecipitation() noexcept { return Precipitation; }
    [[nodiscard]] RainbowInspector&          QueryRainbow() noexcept { return Rainbow; }
    [[nodiscard]] CameraInspector&           QueryCamera() noexcept { return Camera; }
    [[nodiscard]] CameraSettingsInspector&   QueryCameraSettings() noexcept { return CameraSettings; }
    [[nodiscard]] LensFlareInspector&        QueryLensFlare() noexcept { return LensFlare; }
    [[nodiscard]] MotionBlurInspector&         QueryMotionBlur() noexcept { return MotionBlur; }
    [[nodiscard]] PointLightInspector&         QueryPointLight() noexcept { return PointLight; }
    [[nodiscard]] SpotLightInspector&          QuerySpotLight() noexcept { return SpotLight; }
    [[nodiscard]] IesLightInspector&           QueryIesLight() noexcept { return IesLight; }
    [[nodiscard]] AreaLightInspector&          QueryAreaLight() noexcept { return AreaLight; }
    [[nodiscard]] TubeLightInspector&          QueryTubeLight() noexcept { return TubeLight; }
    [[nodiscard]] LedLightInspector&         QueryLedLight() noexcept { return LedLight; }
    [[nodiscard]] LedStripInspector&           QueryLedStrip() noexcept { return LedStrip; }
    [[nodiscard]] EnvironmentLightInspector&   QueryEnvironmentLight() noexcept { return EnvironmentLight; }
    [[nodiscard]] PostProcessInspector&        QueryPostProcess() noexcept { return PostProcess; }
    [[nodiscard]] ExposureInspector&           QueryExposure() noexcept { return Exposure; }
    [[nodiscard]] AnalyticPrimitiveInspector&  QueryAnalyticPrimitive() noexcept { return AnalyticPrimitive; }
    [[nodiscard]] NativeGeometryInspector&     QueryNativeGeometry() noexcept { return NativeGeometry; }
    [[nodiscard]] ImportedGeometryInspector&   QueryImportedGeometry() noexcept { return ImportedGeometry; }
    [[nodiscard]] ProceduralGeometryInspector& QueryProceduralGeometry() noexcept { return ProceduralGeometry; }
    [[nodiscard]] InspectorInstanceRegister&   AccessInstances() noexcept { return Instances; }
    [[nodiscard]] FolderInspector&             QueryFolder() noexcept { return Folder; }

private:
    InspectorInstanceRegister   Instances          {};
    AtmosphereInspector         Atmosphere         {};
    SolarInspector              Solar              {};
    CloudInspector              Cloud              {};
    LocalCloudInspector         LocalCloud         {};
    LunarInspector              Lunar              {};
    StarfieldInspector          Starfield          {};
    WindInspector               Wind               {};
    HeightFogInspector          HeightFog          {};
    AtmosphericFogInspector     AtmosphericFog     {};
    LocalFogInspector           LocalFog           {};
    GodRaysInspector            GodRays            {};
    PrecipitationInspector      Precipitation      {};
    RainbowInspector            Rainbow            {};
    CameraInspector             Camera             {};
    CameraSettingsInspector     CameraSettings     {};
    LensFlareInspector          LensFlare          {};
    MotionBlurInspector         MotionBlur         {};
    PointLightInspector         PointLight         {};
    SpotLightInspector          SpotLight          {};
    IesLightInspector           IesLight           {};
    AreaLightInspector          AreaLight          {};
    TubeLightInspector          TubeLight          {};
    LedLightInspector         LedLight         {};
    LedStripInspector           LedStrip           {};
    EnvironmentLightInspector   EnvironmentLight   {};
    PostProcessInspector        PostProcess        {};
    ExposureInspector           Exposure           {};
    AnalyticPrimitiveInspector  AnalyticPrimitive  {};
    NativeGeometryInspector     NativeGeometry     {};
    ImportedGeometryInspector   ImportedGeometry   {};
    ProceduralGeometryInspector ProceduralGeometry {};
    FolderInspector             Folder             {};
    ProjectInspectorHost        ProjectHost        {};

    uint32_t         LastEntryId            = 0u;
    SmoothScrollView ScrollView             {};
    float            LastPaneWidthPx        = 0.0f;
    uint64_t         ActivePanelRevision    = 1ull;
    uint64_t ConstructCallCount     = 0ull;
    uint32_t LastEvaluatedCardCount = 0u;
    uint32_t LastSkippedCardCount   = 30u;
};

} // namespace Frontier

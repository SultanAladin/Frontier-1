/*============================================================================================================================================
                                                          MATERIALOUTLINERPANE.H
============================================================================================================================================*/
// 🧩 Left-docked Outliner pane for the Frontier Material Workspace: 28 OpenPBR 1.1 channels organized into 6 collapsible groups,
//    search & All/On filter bar, per-channel swatch/bar + toggle switch, and bottom "+ Enable | Preset | Reset · Solo | Mute" footer bar.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Material/MaterialWorkspaceSession.h"
#include "Engine/Workspaces/Texturing/TexturingPaneAppearance.h"

#include <array>
#include <cstdint>
#include <string>

namespace Frontier::Workspaces
{

enum class MaterialChannelGroup : uint8_t
{
    BaseSurface     = 0,
    Microsurface    = 1,
    CoatAndFilm     = 2,
    SheenAndFuzz    = 3,
    TransmissionSss = 4,
    EmissionOpacity = 5,
    Count           = 6,
};

struct MaterialChannelEntrySpec
{
    uint32_t             Index        = 0u;
    MaterialChannelGroup Group        = MaterialChannelGroup::BaseSurface;
    const char*          Label        = "";
    const char*          Subtitle     = "";
    bool                 ColorChannel = false;
    ColorRgba            DefaultTint  { 185u, 122u, 69u, 255u };
    float                DefaultValue = 0.5f;
    const char*          UnitSuffix   = "%";
};

class MaterialOutlinerPane
{
public:
    static constexpr std::size_t ChannelCount = 28u;
    static constexpr std::size_t GroupCount   = 6u;

    MaterialOutlinerPane() noexcept;
    ~MaterialOutlinerPane() noexcept;

    void AttachSession(MaterialWorkspaceSession* TargetSession) noexcept
    {
        Session = TargetSession;
    }

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  DeltaSeconds);

    void PresentInImGuiPane(double DeltaSeconds, float ScaleFactor = 1.0f);

    [[nodiscard]] static const std::array<MaterialChannelEntrySpec, ChannelCount>& QueryChannelCatalog() noexcept;
    [[nodiscard]] static const std::array<const char*, GroupCount>&               QueryGroupNames() noexcept;

    [[nodiscard]] std::size_t CountEnabledChannels() const noexcept;

public:
    std::array<bool, ChannelCount>      ChannelEnabled {};
    std::array<bool, ChannelCount>      ChannelExpanded {};
    std::array<uint8_t, ChannelCount>   ChannelSourceTab {}; // 0: Value, 1: Field, 2: Ramp, 3: Image
    std::array<float, ChannelCount>     ChannelScalars {};
    std::array<ColorRgba, ChannelCount> ChannelTints   {};
    std::array<bool, GroupCount>        GroupOpen      {{ true, true, true, false, false, false }};

    uint32_t SelectedChannelIndex = 0u;
    bool     FilterActiveOnly     = false;
    bool     SoloActive           = false;
    bool     MuteActive           = false;
    float    ScrollOffsetY        = 0.0f;

private:
    MaterialWorkspaceSession* Session    = nullptr;
    float                     MaxScrollY = 0.0f;
};

} // namespace Frontier::Workspaces

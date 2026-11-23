/*============================================================================================================================================
                                                          MATERIALOUTLINERPANE.H
============================================================================================================================================*/
// 🧩 Left-docked Project Zero Outliner pane for the Frontier Material Workspace: Material Scene & Strata header,
//    STRATA / DRIVEN KPI tiles, quick stratum preset chips, hierarchical scene/strata/group/channel tree, action bar, and 4-column telemetry.

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
    Core                   = 0,
    Specular               = 1,
    Coat                   = 2,
    Sheen                  = 3,
    ThinFilm               = 4,
    TransmissionAndEmissive= 5,
    Count                  = 6,
};

struct MaterialChannelEntrySpec
{
    uint32_t             Index        = 0u;
    MaterialChannelGroup Group        = MaterialChannelGroup::Core;
    const char*          Label        = "";
    const char*          Subtitle     = "";
    const char*          KeySlug      = "";
    bool                 ColorChannel = false;
    bool                 NormalChannel= false;
    ColorRgba            DefaultTint  { 9u, 12u, 17u, 255u };
    float                DefaultValue = 0.5f;
    const char*          UnitSuffix   = "";
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
    [[nodiscard]] static const std::array<const char*, GroupCount>&               QueryGroupUpperNames() noexcept;

    [[nodiscard]] std::size_t CountEnabledChannels() const noexcept;
    [[nodiscard]] std::size_t CountDrivenChannels() const noexcept;

public:
    std::array<bool, ChannelCount>      ChannelEnabled   {};
    std::array<bool, ChannelCount>      ChannelExpanded  {};
    std::array<uint8_t, ChannelCount>   ChannelSourceTab {}; // 0: Value, 1: Field, 2: Ramp, 3: Image
    std::array<float, ChannelCount>     ChannelScalars   {};
    std::array<ColorRgba, ChannelCount> ChannelTints     {};
    std::array<bool, GroupCount>        GroupOpen        {{ true, false, false, false, false, false }};
    std::array<bool, GroupCount>        InspectorGroupOpen {{ true, false, false, false, false, false }};

    bool     SceneNodeOpen        = true;
    bool     StratumNodeOpen      = true;
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

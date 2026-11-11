/*============================================================================================================================================
                                                   WORKSPACEDOCKCONTAINER.CPP
============================================================================================================================================*/
// 🧩 Application-scoped domain token issuance and patched Dear ImGui unrounded trapezoidal dockspace builder.

#include "Engine/Components/Docking/WorkspaceDockContainer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Frontier::Components
{

namespace
{

std::atomic<uint64_t> NextApplicationDomainCounter{0x46524E5400000001ULL};

void FormatWindowRootLabel(
    char                      *Buffer,
    std::size_t                BufferSize,
    ApplicationDockDomainToken Domain,
    HostWindowToken            WindowToken) noexcept
{
    std::snprintf(Buffer, BufferSize, "##FrontierDockRoot_%llX_%u",
                  static_cast<unsigned long long>(Domain.Identifier), WindowToken.SlotIndex);
}

void FormatUniquePaneLabel(
    char                      *Buffer,
    std::size_t                BufferSize,
    std::string_view           Title,
    ApplicationDockDomainToken Domain,
    uint32_t                   PaneIdentifier) noexcept
{
    std::snprintf(Buffer, BufferSize, "%.*s##FRNT_%llX_%u", static_cast<int>(Title.size()), Title.data(),
                  static_cast<unsigned long long>(Domain.Identifier), PaneIdentifier);
}

} // namespace

ApplicationDockDomainToken IssueApplicationDockDomain() noexcept
{
    return ApplicationDockDomainToken{NextApplicationDomainCounter.fetch_add(1ULL, std::memory_order_acq_rel)};
}

uint32_t FoldDomainToImGuiClassId(ApplicationDockDomainToken Domain) noexcept
{
    if (!Domain.Valid())
        return 0u;
    const uint32_t Folded = static_cast<uint32_t>(Domain.Identifier ^ (Domain.Identifier >> 32u));
    return (Folded != 0u) ? Folded : 1u;
}

void PresentHostWindowImGuiDockRegion(
    ApplicationDockDomainToken                     DomainToken,
    HostWindowToken                                WindowToken,
    float                                          SurfaceWidthPx,
    float                                          SurfaceHeightPx,
    bool                                           RebuildLayout,
    const std::vector<ImGuiPanePresentationEntry> &PanesInWindow,
    uint32_t                                      *OutAddButtonClickedCount)
{
    if (SurfaceWidthPx <= 1.0f || SurfaceHeightPx <= 1.0f)
        return;

    ImGuiWindowClass ApplicationDockClass{};
    ApplicationDockClass.ClassId                  = FoldDomainToImGuiClassId(DomainToken);
    ApplicationDockClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton;
    ApplicationDockClass.DockingAllowUnclassed    = false;

    char RootWindowName[96]{};
    FormatWindowRootLabel(RootWindowName, sizeof(RootWindowName), DomainToken, WindowToken);

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(SurfaceWidthPx, SurfaceHeightPx));

    constexpr ImGuiWindowFlags RootHostWindowMask =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(RootWindowName, nullptr, RootHostWindowMask);
    ImGui::PopStyleVar(3);

    const ImGuiID RootDockId = ImGui::GetID(RootWindowName);

    if (RebuildLayout || ImGui::DockBuilderGetNode(RootDockId) == nullptr)
    {
        ImGui::DockBuilderRemoveNode(RootDockId);
        ImGui::DockBuilderAddNode(RootDockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(RootDockId, ImVec2(SurfaceWidthPx, SurfaceHeightPx));

        ImGuiID RemainingCenterId = RootDockId;
        ImGuiID LeftRegionId      = 0;
        ImGuiID RightRegionId     = 0;
        ImGuiID TopRegionId       = 0;
        ImGuiID BottomRegionId    = 0;

        for (const auto &Entry : PanesInWindow)
        {
            char PaneLabel[160]{};
            FormatUniquePaneLabel(PaneLabel, sizeof(PaneLabel), Entry.TitleText, DomainToken, Entry.PaneIdentifier);

            const float ClampedRatio = std::clamp(Entry.SplitRatio, 0.10f, 0.85f);
            ImGuiID     TargetId     = RemainingCenterId;

            switch (Entry.Placement)
            {
            case DockPlacementDirection::SplitLeft:
                if (LeftRegionId == 0)
                {
                    LeftRegionId = ImGui::DockBuilderSplitNode(RemainingCenterId, ImGuiDir_Left, ClampedRatio, nullptr,
                                                               &RemainingCenterId);
                }
                TargetId = LeftRegionId;
                break;

            case DockPlacementDirection::SplitRight:
                if (RightRegionId == 0)
                {
                    RightRegionId = ImGui::DockBuilderSplitNode(RemainingCenterId, ImGuiDir_Right, ClampedRatio,
                                                                nullptr, &RemainingCenterId);
                }
                TargetId = RightRegionId;
                break;

            case DockPlacementDirection::SplitTop:
                if (TopRegionId == 0)
                {
                    TopRegionId = ImGui::DockBuilderSplitNode(RemainingCenterId, ImGuiDir_Up, ClampedRatio, nullptr,
                                                              &RemainingCenterId);
                }
                TargetId = TopRegionId;
                break;

            case DockPlacementDirection::SplitBottom:
                if (BottomRegionId == 0)
                {
                    BottomRegionId = ImGui::DockBuilderSplitNode(RemainingCenterId, ImGuiDir_Down, ClampedRatio,
                                                                 nullptr, &RemainingCenterId);
                }
                TargetId = BottomRegionId;
                break;

            case DockPlacementDirection::CenterTabGroup:
            default:
                TargetId = RemainingCenterId;
                break;
            }

            ImGui::DockBuilderDockWindow(PaneLabel, TargetId);
        }

        ImGui::DockBuilderFinish(RootDockId);
    }

    ImGui::DockSpace(RootDockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None, &ApplicationDockClass);
    ImGui::End();

    uint32_t ClickedAddTotal = 0u;
    for (const auto &Entry : PanesInWindow)
    {
        char PaneLabel[160]{};
        FormatUniquePaneLabel(PaneLabel, sizeof(PaneLabel), Entry.TitleText, DomainToken, Entry.PaneIdentifier);

        ImGui::SetNextWindowClass(&ApplicationDockClass);
        bool *ClosePointer = QueryPaneDirectiveEnabled(Entry.Directives, PaneDirectiveMask::EnableCloseButton)
                                 ? Entry.VisibilityToggle
                                 : nullptr;

        if (ImGui::Begin(PaneLabel, ClosePointer, ImGuiWindowFlags_NoCollapse))
        {
            const ImGuiID ActiveDockRegionId = ImGui::GetWindowDockID();
            if (ActiveDockRegionId != 0 &&
                QueryPaneDirectiveEnabled(Entry.Directives, PaneDirectiveMask::EnableTrailingAddButton))
            {
                ImGui::DockNodeSetAddButton(ActiveDockRegionId, true);
                if (ImGui::DockNodeConsumeAddRequest(ActiveDockRegionId))
                    ++ClickedAddTotal;
            }

            if (Entry.ContentPresenter)
                Entry.ContentPresenter();
        }
        ImGui::End();
    }

    if (OutAddButtonClickedCount != nullptr)
        *OutAddButtonClickedCount = ClickedAddTotal;
}

} // namespace Frontier::Components

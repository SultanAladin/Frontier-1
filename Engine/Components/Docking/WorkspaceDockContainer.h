/*============================================================================================================================================
                                                           WORKSPACEDOCKCONTAINER.H
============================================================================================================================================*/
// 🧩 Shared workspace dock container enforcing intra-application multi-window docking and OS window tear-off.

#pragma once

#include "Engine/Components/Docking/WorkspaceTabStrip.h"
#include "Engine/Windowing/PlatformWindow.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Components
{

struct ApplicationDockDomainToken
{
    uint64_t Identifier = 0;

    [[nodiscard]] constexpr bool Valid() const noexcept { return Identifier != 0; }
    [[nodiscard]] constexpr bool operator==(const ApplicationDockDomainToken &) const noexcept = default;
};

struct HostWindowToken
{
    uint32_t SlotIndex = 0;

    [[nodiscard]] constexpr bool operator==(const HostWindowToken &) const noexcept = default;
};

struct WorkspaceDockPaneToken
{
    uint32_t Identifier = 0;

    [[nodiscard]] constexpr bool Valid() const noexcept { return Identifier != 0; }
    [[nodiscard]] constexpr bool operator==(const WorkspaceDockPaneToken &) const noexcept = default;
};

enum class DockPlacementDirection : uint8_t
{
    CenterTabGroup = 0,
    SplitLeft      = 1,
    SplitRight     = 2,
    SplitTop       = 3,
    SplitBottom    = 4,
};

enum class PaneDirectiveMask : uint32_t
{
    EmptyDirectives          = 0u,
    EnableCloseButton        = 1u << 0,
    EnableTrailingAddButton  = 1u << 1,
    EnableOperatingTearOff   = 1u << 2,
    EnableCrossWindowDocking = 1u << 3,
    AssignCentralViewport    = 1u << 4,
    EnableInitialVisibility  = 1u << 5,

    StandardPaneDirectives = EnableCloseButton | EnableTrailingAddButton | EnableOperatingTearOff |
                             EnableCrossWindowDocking | EnableInitialVisibility,
    CentralViewportDirectives = EnableCloseButton | EnableTrailingAddButton | EnableCrossWindowDocking |
                                AssignCentralViewport | EnableInitialVisibility,
};

[[nodiscard]] constexpr PaneDirectiveMask operator|(PaneDirectiveMask Left, PaneDirectiveMask Right) noexcept
{
    return static_cast<PaneDirectiveMask>(static_cast<uint32_t>(Left) | static_cast<uint32_t>(Right));
}

[[nodiscard]] constexpr PaneDirectiveMask operator&(PaneDirectiveMask Left, PaneDirectiveMask Right) noexcept
{
    return static_cast<PaneDirectiveMask>(static_cast<uint32_t>(Left) & static_cast<uint32_t>(Right));
}

[[nodiscard]] constexpr PaneDirectiveMask operator~(PaneDirectiveMask Mask) noexcept
{
    return static_cast<PaneDirectiveMask>(~static_cast<uint32_t>(Mask));
}

[[nodiscard]] constexpr bool QueryPaneDirectiveEnabled(PaneDirectiveMask Mask, PaneDirectiveMask Directive) noexcept
{
    return (static_cast<uint32_t>(Mask) & static_cast<uint32_t>(Directive)) == static_cast<uint32_t>(Directive);
}

enum class DockTransferVerdict : uint8_t
{
    Permitted                  = 0,
    RejectedNullDomain         = 1,
    RejectedForeignApplication = 2,
    RejectedUnknownPane        = 3,
    RejectedUnknownWindow      = 4,
    RejectedTearOffDisabled    = 5,
    RejectedCrossWindowLocked  = 6,
};

using PaneContentPresenter = std::function<void()>;

struct WorkspaceDockPaneDescriptor
{
    std::string_view       PaneTitle        = "Pane";
    DockPlacementDirection InitialDirection = DockPlacementDirection::CenterTabGroup;
    float                  InitialSplitRatio = 0.22f;
    PaneDirectiveMask      Directives       = PaneDirectiveMask::StandardPaneDirectives;
    PaneContentPresenter   ContentPresenter = {};
};

struct WorkspaceDockPaneReading
{
    WorkspaceDockPaneToken Token{};
    HostWindowToken        AssignedWindow{};
    DockPlacementDirection Placement        = DockPlacementDirection::CenterTabGroup;
    float                  SplitRatio       = 0.22f;
    PaneDirectiveMask      Directives       = PaneDirectiveMask::StandardPaneDirectives;
    std::string_view       TitleText{};
    bool                   VisibilityActive = true;
    bool                   DetachedWindow   = false;
};

struct HostWindowReading
{
    HostWindowToken               WindowToken{};
    ApplicationDockDomainToken    DomainToken{};
    Windowing::WindowExtentReading SurfaceExtent{};
    std::string_view              TitleText{};
    uint32_t                      DockedPaneCount = 0;
    bool                          PrimaryWindow   = true;
    bool                          Active          = false;
};

[[nodiscard]] ApplicationDockDomainToken IssueApplicationDockDomain() noexcept;
[[nodiscard]] uint32_t                   FoldDomainToImGuiClassId(ApplicationDockDomainToken Domain) noexcept;

struct ImGuiPanePresentationEntry
{
    uint32_t               PaneIdentifier   = 0;
    std::string_view       TitleText{};
    DockPlacementDirection Placement        = DockPlacementDirection::CenterTabGroup;
    float                  SplitRatio       = 0.22f;
    PaneDirectiveMask      Directives       = PaneDirectiveMask::StandardPaneDirectives;
    PaneContentPresenter   ContentPresenter{};
    bool                  *VisibilityToggle = nullptr;
};

void PresentHostWindowImGuiDockRegion(
    ApplicationDockDomainToken                     DomainToken,
    HostWindowToken                                WindowToken,
    float                                          SurfaceWidthPx,
    float                                          SurfaceHeightPx,
    bool                                           RebuildLayout,
    const std::vector<ImGuiPanePresentationEntry> &PanesInWindow,
    uint32_t                                      *OutAddButtonClickedCount = nullptr);

template <Windowing::PlatformWindowSurface WindowSurfaceType = Windowing::HeadlessPlatformWindow>
class WorkspaceDockContainer
{
public:
    WorkspaceDockContainer() noexcept = default;
    ~WorkspaceDockContainer() noexcept { Finalize(); }

    WorkspaceDockContainer(const WorkspaceDockContainer &)            = delete;
    WorkspaceDockContainer &operator=(const WorkspaceDockContainer &) = delete;

    [[nodiscard]] bool Initialize(
        const Windowing::WindowConfigurationDescriptor &PrimaryWindowDescriptor,
        ApplicationDockDomainToken                      ExplicitDomain = {}) noexcept
    {
        Finalize();

        OwningDomain = ExplicitDomain.Valid() ? ExplicitDomain : IssueApplicationDockDomain();
        TabMetrics   = ConstructWorkspaceTabMetrics(PrimaryWindowDescriptor.DpiScaleFactor);

        auto PrimarySlot         = std::make_unique<HostWindowEntry>();
        PrimarySlot->Token       = HostWindowToken{0u};
        PrimarySlot->Domain      = OwningDomain;
        PrimarySlot->Primary     = true;
        PrimarySlot->LayoutDirty = true;

        if (!PrimarySlot->Surface.Initialize(PrimaryWindowDescriptor))
        {
            OwningDomain = {};
            return false;
        }

        PrimarySlot->Active = true;
        HostWindows.push_back(std::move(PrimarySlot));
        ContainerActive = true;
        return true;
    }

    void PollOperatingWindows(Input::InputExtension &InputCoordinator) noexcept
    {
        if (!ContainerActive)
            return;

        for (std::size_t SlotIdx = 0; SlotIdx < HostWindows.size(); ++SlotIdx)
        {
            auto &Slot = HostWindows[SlotIdx];
            if (!Slot || !Slot->Active)
                continue;
            Slot->Surface.PollOperatingEvents(InputCoordinator);
            if (!Slot->Primary && Slot->Surface.CloseRequested())
                CloseDetachedOperatingWindow(Slot->Token);
        }
    }

    void Finalize() noexcept
    {
        for (auto &Slot : HostWindows)
        {
            if (Slot && Slot->Active)
            {
                Slot->Surface.Finalize();
                Slot->Active = false;
            }
        }
        HostWindows.clear();
        Panes.clear();
        NextPaneId             = 1u;
        RejectedTransfers      = 0u;
        CompletedTearOffCount  = 0u;
        CompletedDockMoves     = 0u;
        AllowDetachedOsWindows = true;
        OwningDomain           = {};
        ContainerActive        = false;
    }

    void ConfigureDetachedWindowsAllowed(bool Allowed) noexcept
    {
        AllowDetachedOsWindows = Allowed;
        if (!Allowed)
            RecallAllDetachedPanesToPrimaryWindow();
    }

    [[nodiscard]] bool QueryDetachedWindowsAllowed() const noexcept { return AllowDetachedOsWindows; }

    void RecallAllDetachedPanesToPrimaryWindow() noexcept
    {
        if (HostWindows.empty() || !HostWindows[0])
            return;
        for (auto &Pane : Panes)
        {
            if (Pane.AssignedWindow.SlotIndex != 0u)
            {
                const HostWindowToken OldWin = Pane.AssignedWindow;
                Pane.AssignedWindow          = HostWindowToken{0u};
                HostWindows[0]->LayoutDirty  = true;
                RetireDetachedWindowIfEmpty(OldWin);
            }
        }
    }

    void ResetPrimaryLayout() noexcept
    {
        RecallAllDetachedPanesToPrimaryWindow();
        if (!HostWindows.empty() && HostWindows[0])
            HostWindows[0]->LayoutDirty = true;
    }

    [[nodiscard]] WorkspaceDockPaneToken RegisterPane(const WorkspaceDockPaneDescriptor &Descriptor) noexcept
    {
        if (!ContainerActive || HostWindows.empty())
            return {};

        PaneEntry Entry{};
        Entry.Token            = WorkspaceDockPaneToken{NextPaneId++};
        Entry.AssignedWindow   = HostWindowToken{0u};
        Entry.Title.assign(Descriptor.PaneTitle.data(), Descriptor.PaneTitle.size());
        Entry.Placement        = Descriptor.InitialDirection;
        Entry.SplitRatio       = Descriptor.InitialSplitRatio;
        Entry.Directives       = Descriptor.Directives;
        Entry.ContentPresenter = Descriptor.ContentPresenter;
        Entry.VisibilityActive = QueryPaneDirectiveEnabled(
            Descriptor.Directives, PaneDirectiveMask::EnableInitialVisibility);

        Panes.push_back(std::move(Entry));
        HostWindows[0]->LayoutDirty = true;
        return Panes.back().Token;
    }

    [[nodiscard]] DockTransferVerdict VerifyDockTransfer(
        WorkspaceDockPaneToken     PaneToken,
        ApplicationDockDomainToken CandidateTargetDomain,
        HostWindowToken            CandidateTargetWindow) const noexcept
    {
        if (!OwningDomain.Valid() || !CandidateTargetDomain.Valid())
            return DockTransferVerdict::RejectedNullDomain;
        if (CandidateTargetDomain != OwningDomain)
            return DockTransferVerdict::RejectedForeignApplication;

        const PaneEntry *Pane = LocatePane(PaneToken);
        if (Pane == nullptr)
            return DockTransferVerdict::RejectedUnknownPane;

        if (CandidateTargetWindow.SlotIndex >= HostWindows.size() ||
            !HostWindows[CandidateTargetWindow.SlotIndex] ||
            !HostWindows[CandidateTargetWindow.SlotIndex]->Active)
        {
            return DockTransferVerdict::RejectedUnknownWindow;
        }

        if (Pane->AssignedWindow != CandidateTargetWindow &&
            !QueryPaneDirectiveEnabled(Pane->Directives, PaneDirectiveMask::EnableCrossWindowDocking))
        {
            return DockTransferVerdict::RejectedCrossWindowLocked;
        }

        return DockTransferVerdict::Permitted;
    }

    [[nodiscard]] DockTransferVerdict DockPaneToApplicationWindow(
        WorkspaceDockPaneToken     PaneToken,
        ApplicationDockDomainToken TargetDomain,
        HostWindowToken            TargetWindow,
        DockPlacementDirection     Direction  = DockPlacementDirection::CenterTabGroup,
        float                      SplitRatio = 0.25f) noexcept
    {
        const DockTransferVerdict Verdict = VerifyDockTransfer(PaneToken, TargetDomain, TargetWindow);
        if (Verdict != DockTransferVerdict::Permitted)
        {
            ++RejectedTransfers;
            return Verdict;
        }

        PaneEntry            *Pane      = LocatePaneMutable(PaneToken);
        const HostWindowToken OldWindow = Pane->AssignedWindow;

        Pane->AssignedWindow   = TargetWindow;
        Pane->Placement        = Direction;
        Pane->SplitRatio       = SplitRatio;
        Pane->VisibilityActive = true;

        HostWindows[TargetWindow.SlotIndex]->LayoutDirty = true;
        if (OldWindow.SlotIndex < HostWindows.size() && HostWindows[OldWindow.SlotIndex])
        {
            HostWindows[OldWindow.SlotIndex]->LayoutDirty = true;
            RetireDetachedWindowIfEmpty(OldWindow);
        }

        ++CompletedDockMoves;
        return DockTransferVerdict::Permitted;
    }

    [[nodiscard]] DockTransferVerdict DockPaneToWindow(
        WorkspaceDockPaneToken PaneToken,
        HostWindowToken        TargetWindow,
        DockPlacementDirection Direction  = DockPlacementDirection::CenterTabGroup,
        float                  SplitRatio = 0.25f) noexcept
    {
        return DockPaneToApplicationWindow(PaneToken, OwningDomain, TargetWindow, Direction, SplitRatio);
    }

    [[nodiscard]] HostWindowToken DetachPaneToOperatingWindow(
        WorkspaceDockPaneToken PaneToken,
        int32_t                ScreenOriginX = 260,
        int32_t                ScreenOriginY = 140,
        uint32_t               PixelWidth    = 460,
        uint32_t               PixelHeight   = 620) noexcept
    {
        PaneEntry *Pane = LocatePaneMutable(PaneToken);
        if (Pane == nullptr || !AllowDetachedOsWindows ||
            !QueryPaneDirectiveEnabled(Pane->Directives, PaneDirectiveMask::EnableOperatingTearOff))
        {
            ++RejectedTransfers;
            return HostWindowToken{0u};
        }

        const uint32_t TargetSlotIndex = FindOrAllocateWindowSlot();
        auto          &DetachedSlot    = HostWindows[TargetSlotIndex];
        DetachedSlot                   = std::make_unique<HostWindowEntry>();
        DetachedSlot->Token            = HostWindowToken{TargetSlotIndex};
        DetachedSlot->Domain           = OwningDomain;
        DetachedSlot->Primary          = false;
        DetachedSlot->LayoutDirty      = true;

        const auto WindowDescriptor = Windowing::WindowConfigurationDescriptor::ConstructDetachedPanelWindow(
            Pane->Title, ScreenOriginX, ScreenOriginY, PixelWidth, PixelHeight);

        if (!DetachedSlot->Surface.Initialize(WindowDescriptor))
        {
            DetachedSlot.reset();
            return HostWindowToken{0u};
        }
        DetachedSlot->Active = true;

        const HostWindowToken PreviousWindow = Pane->AssignedWindow;
        Pane->AssignedWindow                 = DetachedSlot->Token;
        Pane->Placement                      = DockPlacementDirection::CenterTabGroup;
        Pane->SplitRatio                     = 1.0f;
        Pane->VisibilityActive               = true;

        if (PreviousWindow.SlotIndex < HostWindows.size() && HostWindows[PreviousWindow.SlotIndex])
        {
            HostWindows[PreviousWindow.SlotIndex]->LayoutDirty = true;
            RetireDetachedWindowIfEmpty(PreviousWindow);
        }

        ++CompletedTearOffCount;
        return DetachedSlot->Token;
    }

    void TogglePaneVisibility(WorkspaceDockPaneToken PaneToken, bool EnableVisible) noexcept
    {
        if (PaneEntry *Pane = LocatePaneMutable(PaneToken))
        {
            Pane->VisibilityActive = EnableVisible;
            if (Pane->AssignedWindow.SlotIndex < HostWindows.size() && HostWindows[Pane->AssignedWindow.SlotIndex])
            {
                HostWindows[Pane->AssignedWindow.SlotIndex]->LayoutDirty = true;
                if (!EnableVisible)
                    RetireDetachedWindowIfEmpty(Pane->AssignedWindow);
            }
        }
    }

    void PresentWindowImGuiPanes(HostWindowToken WindowToken, uint32_t *OutAddButtonClicks = nullptr)
    {
        if (WindowToken.SlotIndex >= HostWindows.size() ||
            !HostWindows[WindowToken.SlotIndex] ||
            !HostWindows[WindowToken.SlotIndex]->Active)
        {
            return;
        }

        auto                                   &Slot   = *HostWindows[WindowToken.SlotIndex];
        const auto                              Extent = Slot.Surface.InspectExtent();
        std::vector<ImGuiPanePresentationEntry> WindowPanes;
        WindowPanes.reserve(Panes.size());

        for (auto &Pane : Panes)
        {
            if (Pane.AssignedWindow == WindowToken && Pane.VisibilityActive)
            {
                ImGuiPanePresentationEntry PresentationEntry{};
                PresentationEntry.PaneIdentifier   = Pane.Token.Identifier;
                PresentationEntry.TitleText        = Pane.Title;
                PresentationEntry.Placement        = Pane.Placement;
                PresentationEntry.SplitRatio       = Pane.SplitRatio;
                PresentationEntry.Directives       = Pane.Directives;
                PresentationEntry.ContentPresenter = Pane.ContentPresenter;
                PresentationEntry.VisibilityToggle = &Pane.VisibilityActive;
                WindowPanes.push_back(std::move(PresentationEntry));
            }
        }

        PresentHostWindowImGuiDockRegion(
            OwningDomain, WindowToken, static_cast<float>(Extent.PixelWidth), static_cast<float>(Extent.PixelHeight),
            Slot.LayoutDirty, WindowPanes, OutAddButtonClicks);

        Slot.LayoutDirty = false;
        RetireDetachedWindowIfEmpty(WindowToken);
    }

    [[nodiscard]] ApplicationDockDomainToken DomainToken() const noexcept            { return OwningDomain; }
    [[nodiscard]] WorkspaceTabMetrics        ActiveTabMetrics() const noexcept       { return TabMetrics; }
    [[nodiscard]] uint32_t                   RejectedTransferCount() const noexcept  { return RejectedTransfers; }
    [[nodiscard]] uint32_t                   TearOffOperatingCount() const noexcept  { return CompletedTearOffCount; }
    [[nodiscard]] uint32_t                   CompletedDockMoveCount() const noexcept { return CompletedDockMoves; }

    [[nodiscard]] uint32_t ActiveOperatingWindowCount() const noexcept
    {
        uint32_t Count = 0u;
        for (const auto &Slot : HostWindows)
        {
            if (Slot && Slot->Active)
                ++Count;
        }
        return Count;
    }

    [[nodiscard]] std::size_t RegisteredPaneCount() const noexcept { return Panes.size(); }

    [[nodiscard]] WorkspaceDockPaneReading InspectPane(WorkspaceDockPaneToken PaneToken) const noexcept
    {
        WorkspaceDockPaneReading Reading{};
        const PaneEntry         *Pane = LocatePane(PaneToken);
        if (Pane == nullptr)
            return Reading;

        Reading.Token            = Pane->Token;
        Reading.AssignedWindow   = Pane->AssignedWindow;
        Reading.Placement        = Pane->Placement;
        Reading.SplitRatio       = Pane->SplitRatio;
        Reading.Directives       = Pane->Directives;
        Reading.TitleText        = Pane->Title;
        Reading.VisibilityActive = Pane->VisibilityActive;
        Reading.DetachedWindow   = (Pane->AssignedWindow.SlotIndex != 0u);
        return Reading;
    }

    [[nodiscard]] HostWindowReading InspectHostWindow(HostWindowToken WindowToken) const noexcept
    {
        HostWindowReading Reading{};
        if (WindowToken.SlotIndex >= HostWindows.size() ||
            !HostWindows[WindowToken.SlotIndex] ||
            !HostWindows[WindowToken.SlotIndex]->Active)
        {
            return Reading;
        }

        const auto &Slot        = *HostWindows[WindowToken.SlotIndex];
        Reading.WindowToken     = Slot.Token;
        Reading.DomainToken     = Slot.Domain;
        Reading.SurfaceExtent   = Slot.Surface.InspectExtent();
        Reading.TitleText       = Slot.Surface.TitleText();
        Reading.DockedPaneCount = CountVisiblePanesInWindow(WindowToken);
        Reading.PrimaryWindow   = Slot.Primary;
        Reading.Active          = Slot.Active;
        return Reading;
    }

    [[nodiscard]] WindowSurfaceType *AccessOperatingSurface(HostWindowToken WindowToken) noexcept
    {
        if (WindowToken.SlotIndex >= HostWindows.size() ||
            !HostWindows[WindowToken.SlotIndex] ||
            !HostWindows[WindowToken.SlotIndex]->Active)
        {
            return nullptr;
        }
        return &HostWindows[WindowToken.SlotIndex]->Surface;
    }

private:
    struct HostWindowEntry
    {
        HostWindowToken            Token{};
        ApplicationDockDomainToken Domain{};
        WindowSurfaceType          Surface{};
        bool                       Primary     = false;
        bool                       Active      = false;
        bool                       LayoutDirty = true;
    };

    struct PaneEntry
    {
        WorkspaceDockPaneToken Token{};
        HostWindowToken        AssignedWindow{};
        std::string            Title{};
        DockPlacementDirection Placement        = DockPlacementDirection::CenterTabGroup;
        float                  SplitRatio       = 0.22f;
        PaneDirectiveMask      Directives       = PaneDirectiveMask::StandardPaneDirectives;
        PaneContentPresenter   ContentPresenter{};
        bool                   VisibilityActive = true;
    };

    [[nodiscard]] const PaneEntry *LocatePane(WorkspaceDockPaneToken Token) const noexcept
    {
        for (const auto &Entry : Panes)
        {
            if (Entry.Token == Token)
                return &Entry;
        }
        return nullptr;
    }

    [[nodiscard]] PaneEntry *LocatePaneMutable(WorkspaceDockPaneToken Token) noexcept
    {
        for (auto &Entry : Panes)
        {
            if (Entry.Token == Token)
                return &Entry;
        }
        return nullptr;
    }

    [[nodiscard]] uint32_t CountVisiblePanesInWindow(HostWindowToken WindowToken) const noexcept
    {
        uint32_t Count = 0u;
        for (const auto &Entry : Panes)
        {
            if (Entry.AssignedWindow == WindowToken && Entry.VisibilityActive)
                ++Count;
        }
        return Count;
    }

    [[nodiscard]] uint32_t FindOrAllocateWindowSlot() noexcept
    {
        for (uint32_t Index = 1u; Index < static_cast<uint32_t>(HostWindows.size()); ++Index)
        {
            if (!HostWindows[Index] || !HostWindows[Index]->Active)
                return Index;
        }
        HostWindows.push_back(nullptr);
        return static_cast<uint32_t>(HostWindows.size() - 1u);
    }

    void RetireDetachedWindowIfEmpty(HostWindowToken WindowToken) noexcept
    {
        if (WindowToken.SlotIndex == 0u || WindowToken.SlotIndex >= HostWindows.size())
            return;
        auto &Slot = HostWindows[WindowToken.SlotIndex];
        if (!Slot || !Slot->Active || Slot->Primary)
            return;
        if (CountVisiblePanesInWindow(WindowToken) == 0u)
        {
            Slot->Surface.Finalize();
            Slot->Active = false;
            Slot.reset();
        }
    }

    void CloseDetachedOperatingWindow(HostWindowToken WindowToken) noexcept
    {
        if (WindowToken.SlotIndex == 0u || WindowToken.SlotIndex >= HostWindows.size())
            return;
        for (auto &Pane : Panes)
        {
            if (Pane.AssignedWindow == WindowToken)
            {
                Pane.VisibilityActive = false;
                Pane.AssignedWindow   = HostWindowToken{0u};
            }
        }
        RetireDetachedWindowIfEmpty(WindowToken);
    }

    std::vector<std::unique_ptr<HostWindowEntry>> HostWindows{};
    std::vector<PaneEntry>                        Panes{};
    ApplicationDockDomainToken                    OwningDomain{};
    WorkspaceTabMetrics                           TabMetrics{};
    uint32_t                                      NextPaneId             = 1u;
    uint32_t                                      RejectedTransfers      = 0u;
    uint32_t                                      CompletedTearOffCount  = 0u;
    uint32_t                                      CompletedDockMoves     = 0u;
    bool                                          AllowDetachedOsWindows = true;
    bool                                          ContainerActive        = false;
};

using HeadlessWorkspaceDockContainer = WorkspaceDockContainer<Windowing::HeadlessPlatformWindow>;
#if !defined(__ANDROID__)
using DesktopWorkspaceDockContainer = WorkspaceDockContainer<Windowing::DesktopPlatformWindow>;
#endif

} // namespace Frontier::Components

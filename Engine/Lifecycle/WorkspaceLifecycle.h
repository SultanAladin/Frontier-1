/*============================================================================================================================================
                                                           WORKSPACELIFECYCLE.H
============================================================================================================================================*/
// 🧩 Multi-document workspace session loop driving active workspace tabs, document revisions, and shared Components/Panes

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/InspectorPanel.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/Panes/ViewportPanel.h"
#include "Engine/Lifecycle/LifecycleSequence.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Lifecycle
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct WorkspaceSessionToken
{
    uint32_t SlotIndex  = 0u; // [idx]   - Session slot index (1-based; 0 = invalid)
    uint32_t Generation = 0u; // [count] - Generation tag preventing stale-token reuse

    [[nodiscard]] constexpr bool Valid() const noexcept
    {
        return SlotIndex != 0u && Generation != 0u;
    }

    [[nodiscard]] constexpr bool operator==(const WorkspaceSessionToken&) const noexcept = default;
};

struct WorkspaceDocumentDescriptor
{
    std::string_view      DocumentTitle  = "Untitled";                    // [-] - Tab and viewport scene heading
    OutlinerTitleVariant  OutlinerVariant = OutlinerTitleVariant::Outliner;// [-] - Outliner vs LayerStack pane heading
    ViewportExecutionMode InitialMode    = ViewportExecutionMode::Edit;   // [-] - Initial Edit/Simulate/Play mode
};

struct WorkspaceSessionReading
{
    WorkspaceSessionToken Token            {};                            // [idx]   - Issued generation-checked session token
    std::string_view      DocumentTitle    {};                            // [-]     - Current document title
    OutlinerTitleVariant  OutlinerVariant  = OutlinerTitleVariant::Outliner; // [-]  - Active Outliner heading mode
    ViewportExecutionMode ExecutionMode    = ViewportExecutionMode::Edit; // [-]     - Active Viewport execution mode
    uint32_t              RevisionCursor   = 0u;                          // [idx]   - Current cursor in revision timeline
    uint32_t              RevisionTotal    = 0u;                          // [count] - Total recorded revisions in branch
    uint32_t              ConstructedSlots = 0u;                          // [count] - Total Outliner entries in document
    bool                  Modified         = false;                       // [-]     - Unsaved revision indicator
    bool                  ActiveTab        = false;                       // [-]     - True when session is frontmost tab
};

struct WorkspaceSessionPanes
{
    OutlinerPanel  Outliner  {}; // [-] - Bound Outliner / LayerStack tree pane (0 initial entries)
    InspectorPanel Inspector {}; // [-] - Bound Showcase Folder / Entry Inspector pane
    ViewportPanel  Viewport  {}; // [-] - Bound Viewport toolbar, Construct & Diagnostics pane
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class WorkspaceLifecycle
{
public:
    WorkspaceLifecycle() noexcept = default;
    ~WorkspaceLifecycle() noexcept;

    WorkspaceLifecycle(const WorkspaceLifecycle&)            = delete;
    WorkspaceLifecycle& operator=(const WorkspaceLifecycle&) = delete;

    template <LifecycleParticipant ParticipantType>
    [[nodiscard]] uint32_t RegisterWorkspaceParticipant(std::string_view Identifier, ParticipantType& Participant)
    {
        return SessionSequence.RegisterParticipant(Identifier, Participant);
    }

    [[nodiscard]] bool Initialize(bool OpenDefaultEmptySession = true) noexcept;
    void               Tick(double Δt) noexcept;
    [[nodiscard]] bool Suspend() noexcept;
    [[nodiscard]] bool Resume() noexcept;
    void               Finalize() noexcept;

    [[nodiscard]] WorkspaceSessionToken OpenSession(const WorkspaceDocumentDescriptor& Descriptor = {});
    [[nodiscard]] bool                  CloseSession(WorkspaceSessionToken Token) noexcept;
    [[nodiscard]] bool                  ActivateSession(WorkspaceSessionToken Token) noexcept;
    void                                RenameSession(WorkspaceSessionToken Token, std::string_view NewTitle);

    [[nodiscard]] uint32_t RecordRevision(WorkspaceSessionToken Token, std::string_view RevisionSummary);
    [[nodiscard]] bool     StepBackwardRevision(WorkspaceSessionToken Token) noexcept;
    [[nodiscard]] bool     StepForwardRevision(WorkspaceSessionToken Token) noexcept;
    void                   MarkSessionSaved(WorkspaceSessionToken Token) noexcept;

    void PresentActivePanesInImGui(double Δt, float ScaleFactor = 1.0f);

    [[nodiscard]] LifecyclePhase                       QueryPhase() const noexcept;
    [[nodiscard]] const LifecycleStepMetrics&          InspectMetrics() const noexcept;
    [[nodiscard]] WorkspaceSessionToken                QueryActiveSession() const noexcept;
    [[nodiscard]] uint32_t                             OpenSessionCount() const noexcept;
    [[nodiscard]] std::vector<WorkspaceSessionToken>   EnumerateOpenSessions() const;
    [[nodiscard]] WorkspaceSessionReading              InspectSession(WorkspaceSessionToken Token) const noexcept;
    [[nodiscard]] WorkspaceSessionPanes*               AccessSessionPanes(WorkspaceSessionToken Token) noexcept;
    [[nodiscard]] const WorkspaceSessionPanes*         InspectSessionPanes(WorkspaceSessionToken Token) const noexcept;
    [[nodiscard]] WorkspaceSessionPanes*               AccessActivePanes() noexcept;

private:
    struct RevisionEntry
    {
        uint32_t    RevisionIndex = 0u;  // [idx] - 1-based revision ordinal
        std::string Summary       {};    // [-]   - Compact edit summary label
    };

    struct SessionSlot
    {
        uint32_t                               Generation        = 1u;   // [count] - Slot generation counter
        bool                                   Occupied          = false;// [-]     - True while document session is open
        std::string                            Title             {};     // [-]     - Document display title
        OutlinerTitleVariant                   TitleVariant      = OutlinerTitleVariant::Outliner; // [-] - Tree heading mode
        std::unique_ptr<WorkspaceSessionPanes> Panes             {};     // [-]     - Cross-wired Outliner, Inspector & Viewport
        std::vector<RevisionEntry>             Revisions         {};     // [-]     - Linear/truncated revision timeline
        uint32_t                               RevisionCursor    = 0u;   // [idx]   - Current active revision ordinal
        uint32_t                               SavedRevisionMark = 0u;   // [idx]   - Revision ordinal last saved to disk
    };

    [[nodiscard]] SessionSlot*       LocateMutable(WorkspaceSessionToken Token) noexcept;
    [[nodiscard]] const SessionSlot* LocateConst(WorkspaceSessionToken Token) const noexcept;

    std::vector<SessionSlot> Slots              {};     // [-]     - Indexed workspace document slots
    WorkspaceSessionToken    ActiveSessionToken {};     // [idx]   - Frontmost active workspace tab token
    LifecycleSequence        SessionSequence    {};     // [-]     - Registered workspace-level participants
    double                   LatestStepSeconds  = 0.0;  // [s]     - Most recent Tick step duration
    bool                     Initialized        = false;// [-]     - True between Initialize and Finalize
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline LifecyclePhase WorkspaceLifecycle::QueryPhase() const noexcept
{
    return SessionSequence.QueryPhase();
}

inline const LifecycleStepMetrics& WorkspaceLifecycle::InspectMetrics() const noexcept
{
    return SessionSequence.InspectMetrics();
}

inline WorkspaceSessionToken WorkspaceLifecycle::QueryActiveSession() const noexcept
{
    return ActiveSessionToken;
}

} // namespace Frontier::Lifecycle

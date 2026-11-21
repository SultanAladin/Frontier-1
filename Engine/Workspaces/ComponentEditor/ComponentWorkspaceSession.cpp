/*============================================================================================================================================
                                                       COMPONENTWORKSPACESESSION.CPP
============================================================================================================================================*/
// 🧩 Implementation of ComponentWorkspaceSession (documents, catalog, compilation, icon baking, Jolt/VehicleDynamics simulation)

#include "Engine/Workspaces/ComponentEditor/ComponentWorkspaceSession.h"
#include "Engine/GeometricRaster/ComponentEditor/ComponentSpecimenBaker.h"
#include "Engine/Interchange/GeometryInterchange.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Workspaces::ComponentEditor
{

ComponentWorkspaceSession::ComponentWorkspaceSession() = default;
ComponentWorkspaceSession::~ComponentWorkspaceSession() = default;

void ComponentWorkspaceSession::InitializeDefaultWorkspace(const std::filesystem::path& RepositoryRoot)
{
    RepoRoot = RepositoryRoot;
    Documents.clear();
    Repository.clear();
    AssetIcons.clear();

    const std::filesystem::path ModelsDir = RepositoryRoot / "Experimental/Shared/Models";
    ComponentClass::ControlVehicleBuildResult Build {};
    std::string Diag {};
    if (ComponentClass::ControlVehicleClassBuilder::ConstructControlVehicle(ModelsDir, Build, Diag))
    {
        Repository = std::move(Build.GeometryRepository);

        ComponentClassDocument Doc {};
        Doc.ClassDefinition = std::move(Build.ClassDefinition);
        Doc.SelectedToken   = Doc.ClassDefinition.Storage.QueryOriginToken();
        Doc.Diagnostics     = ComponentClass::ComponentClassValidator::ValidateClass(
            Doc.ClassDefinition,
            [&](std::string_view AssetId)
            {
                return std::any_of(Repository.begin(), Repository.end(), [&](const auto& E) { return E.AssetIdentifier == AssetId; });
            });
        Doc.CompilePermitted      = !ComponentClass::ComponentClassValidator::QueryHasBlockingErrors(Doc.Diagnostics);
        Doc.Dirty           = false;
        Doc.Camera.FocusPointMeters = { 0.0f, 0.55f, 0.0f };
        Doc.Camera.YawRadians       = 0.72f;
        Doc.Camera.PitchRadians     = 0.28f;
        Doc.Camera.DistanceMeters   = 7.20f;
        SyncEditPoseFromClass(Doc);
        Documents.push_back(std::move(Doc));
        ActiveDocIndex = 0u;
        StatusNote     = "ControlVehicle loaded (" + std::to_string(Repository.size()) + " assets)";
    }
    else
    {
        CreateNewClassDocument("ControlVehicle");
        StatusNote = Diag.empty() ? "Initialized fallback ComponentClass" : Diag;
    }

    if (BoundRenderer != nullptr && BoundRenderer->QueryVulkanReady())
    {
        BakePendingThumbnails();
    }
}

void ComponentWorkspaceSession::AttachRenderer(
    GeometricRaster::ComponentEditor::ComponentViewportRenderer* RendererPtr) noexcept
{
    BoundRenderer = RendererPtr;
    VulkanReady   = (BoundRenderer != nullptr && BoundRenderer->QueryVulkanReady());
    if (VulkanReady)
    {
        BakePendingThumbnails();
    }
}

ComponentClassDocument* ComponentWorkspaceSession::AccessActiveDocument() noexcept
{
    if (Documents.empty() || ActiveDocIndex >= Documents.size())
    {
        return nullptr;
    }
    return &Documents[ActiveDocIndex];
}

const ComponentClassDocument* ComponentWorkspaceSession::InspectActiveDocument() const noexcept
{
    if (Documents.empty() || ActiveDocIndex >= Documents.size())
    {
        return nullptr;
    }
    return &Documents[ActiveDocIndex];
}

void ComponentWorkspaceSession::SelectTab(std::size_t Index) noexcept
{
    if (Index < Documents.size())
    {
        ActiveDocIndex = Index;
        if (auto* Doc = AccessActiveDocument())
        {
            SyncEditPoseFromClass(*Doc);
        }
        ++RenderMutator;
    }
}

void ComponentWorkspaceSession::CloseTab(std::size_t Index)
{
    if (Documents.size() <= 1u || Index >= Documents.size())
    {
        return;
    }
    Documents.erase(Documents.begin() + static_cast<std::ptrdiff_t>(Index));
    if (ActiveDocIndex >= Documents.size())
    {
        ActiveDocIndex = Documents.size() - 1u;
    }
    if (auto* Doc = AccessActiveDocument())
    {
        SyncEditPoseFromClass(*Doc);
    }
    ++RenderMutator;
}

void ComponentWorkspaceSession::CreateNewClassDocument(std::string_view Title, std::string_view InitialAssetId)
{
    ComponentClassDocument Doc {};
    Doc.ClassDefinition.Title     = std::string(Title.empty() ? "ComponentClass" : Title);
    Doc.ClassDefinition.ClassArchetype = ComponentClass::ComponentClassArchetype::StaticMechanicalProp;
    Doc.ClassDefinition.Storage.ResetWithSpatialOrigin("Origin");
    const auto Origin = Doc.ClassDefinition.Storage.QueryOriginToken();
    Doc.SelectedToken = Origin;

    if (!InitialAssetId.empty())
    {
        const auto Tok = Doc.ClassDefinition.Storage.AppendInstance(
            ComponentClass::ComponentCategory::GeometryInstance,
            InitialAssetId,
            Origin);
        if (auto* Geom = Doc.ClassDefinition.Storage.ResolveGeometry(Tok))
        {
            Geom->AssetIdentifier = std::string(InitialAssetId);
            Doc.SelectedToken     = Tok;
        }
    }
    Doc.Diagnostics = ComponentClass::ComponentClassValidator::ValidateClass(
        Doc.ClassDefinition,
        [&](std::string_view AssetId)
        {
            return std::any_of(Repository.begin(), Repository.end(), [&](const auto& E) { return E.AssetIdentifier == AssetId; });
        });
    Doc.CompilePermitted  = !ComponentClass::ComponentClassValidator::QueryHasBlockingErrors(Doc.Diagnostics);
    Doc.Camera     = (BoundRenderer != nullptr && BoundRenderer->QueryVulkanReady())
        ? BoundRenderer->ComputeFramedCameraForClass(Doc.ClassDefinition, Repository)
        : GeometricRaster::ComponentEditor::ComponentViewportCamera{};
    Documents.push_back(std::move(Doc));
    ActiveDocIndex = Documents.size() - 1u;
    SyncEditPoseFromClass(Documents[ActiveDocIndex]);
    BakePendingThumbnails();
    ++RenderMutator;
}

void ComponentWorkspaceSession::MarkActiveDirty() noexcept
{
    if (auto* Doc = AccessActiveDocument())
    {
        Doc->Dirty = true;
        if (!SimulationActive)
        {
            SyncEditPoseFromClass(*Doc);
        }
    }
    ++RenderMutator;
}

void ComponentWorkspaceSession::CompileActiveDocument()
{
    auto* Doc = AccessActiveDocument();
    if (Doc == nullptr)
    {
        return;
    }
    Doc->ClassDefinition.FinalizeCompiledRevision();
    Doc->Diagnostics = ComponentClass::ComponentClassValidator::ValidateClass(
        Doc->ClassDefinition,
        [&](std::string_view AssetId)
        {
            return std::any_of(Repository.begin(), Repository.end(), [&](const auto& E) { return E.AssetIdentifier == AssetId; });
        });
    Doc->CompilePermitted = !ComponentClass::ComponentClassValidator::QueryHasBlockingErrors(Doc->Diagnostics);
    Doc->Dirty      = false;
    Doc->IconBaked  = false;
    BakePendingThumbnails();
    StatusNote = Doc->CompilePermitted
        ? ("Compiled rev " + std::to_string(Doc->ClassDefinition.CompiledRevision))
        : ("Compile issues: " + std::to_string(Doc->Diagnostics.size()));
    ++RenderMutator;
}

void ComponentWorkspaceSession::BakePendingThumbnails()
{
    if (BoundRenderer == nullptr || !BoundRenderer->QueryVulkanReady())
    {
        return;
    }

    for (const auto& Entry : Repository)
    {
        if (AssetIcons.contains(Entry.AssetIdentifier))
        {
            continue;
        }
        std::vector<uint8_t> Pixels {};
        if (GeometricRaster::ComponentEditor::ComponentSpecimenBaker::BakeGeometryAssetThumbnail(
                *BoundRenderer, Entry, Pixels, 128u))
        {
            AssetIcons[Entry.AssetIdentifier] = { 128u, 128u, std::move(Pixels) };
        }
    }

    for (auto& Doc : Documents)
    {
        if (!Doc.IconBaked || Doc.ClassDefinition.BakedIconRgba8.empty())
        {
            Doc.IconBaked = GeometricRaster::ComponentEditor::ComponentSpecimenBaker::BakeClassThumbnail(
                *BoundRenderer, Doc.ClassDefinition, Repository, 128u);
        }
    }
}

bool ComponentWorkspaceSession::ImportGeometryFile(const std::filesystem::path& FilePath, std::string& OutDiagnostic)
{
    GeometrySpace::ConstructedSampleAsset ImportedAsset {};
    if (!Interchange::GeometryInterchange::ImportAssetFromFile(FilePath, ImportedAsset, OutDiagnostic))
    {
        if (OutDiagnostic.empty())
        {
            OutDiagnostic = "Geometry import failed.";
        }
        StatusNote = OutDiagnostic;
        return false;
    }

    std::string PrimaryId = FilePath.stem().string();
    if (PrimaryId.empty())
    {
        PrimaryId = "ImportedGeometry";
    }
    std::string UniqueId = PrimaryId;
    uint32_t Suffix = 1u;
    while (std::any_of(Repository.begin(), Repository.end(), [&](const auto& E) { return E.AssetIdentifier == UniqueId; }))
    {
        UniqueId = PrimaryId + "_" + std::to_string(++Suffix);
    }

    ComponentClass::CompiledGeometryRepositoryEntry Entry {};
    Entry.AssetIdentifier = UniqueId;
    Entry.DisplayTitle    = UniqueId;
    Entry.SourceFormat    = FilePath.extension().string();
    Entry.Asset           = std::move(ImportedAsset);
    Repository.push_back(std::move(Entry));

    if (auto* Doc = AccessActiveDocument())
    {
        const auto Encl = Doc->SelectedToken.Valid() ? Doc->SelectedToken : Doc->ClassDefinition.Storage.QueryOriginToken();
        const auto Tok = Doc->ClassDefinition.Storage.AppendInstance(
            ComponentClass::ComponentCategory::GeometryInstance,
            UniqueId,
            Encl);
        if (auto* Geom = Doc->ClassDefinition.Storage.ResolveGeometry(Tok))
        {
            Geom->AssetIdentifier = UniqueId;
            Doc->SelectedToken    = Tok;
            Doc->Dirty            = true;
        }
    }
    BakePendingThumbnails();
    StatusNote = "Imported " + UniqueId;
    ++RenderMutator;
    return true;
}

void ComponentWorkspaceSession::AddComponentToActive(
    ComponentClass::ComponentCategory     Category,
    std::string_view                      Title,
    ComponentClass::PhysicsConstraintCategory ConstraintCategory,
    std::string_view                      AssetId)
{
    auto* Doc = AccessActiveDocument();
    if (Doc == nullptr)
    {
        return;
    }

    ComponentClass::ComponentInstanceToken Enclosing {};
    if (Category != ComponentClass::ComponentCategory::VehicleDynamics &&
        Category != ComponentClass::ComponentCategory::RotationalMotion)
    {
        if (Doc->SelectedToken.Valid())
        {
            if (const auto* SelHdr = Doc->ClassDefinition.Storage.InspectHeader(Doc->SelectedToken);
                SelHdr != nullptr && SelHdr->Category != ComponentClass::ComponentCategory::VehicleDynamics &&
                SelHdr->Category != ComponentClass::ComponentCategory::RotationalMotion)
            {
                Enclosing = Doc->SelectedToken;
            }
        }
        if (!Enclosing.Valid())
        {
            Enclosing = Doc->ClassDefinition.Storage.QueryOriginToken();
        }
    }

    const auto Tok = Doc->ClassDefinition.Storage.AppendInstance(Category, Title, Enclosing);
    if (!Tok.Valid())
    {
        return;
    }

    if (Category == ComponentClass::ComponentCategory::GeometryInstance)
    {
        if (auto* G = Doc->ClassDefinition.Storage.ResolveGeometry(Tok))
        {
            G->AssetIdentifier = !AssetId.empty()
                ? std::string(AssetId)
                : (Repository.empty() ? std::string() : Repository.front().AssetIdentifier);
        }
    }
    else if (Category == ComponentClass::ComponentCategory::ConstraintInstance)
    {
        if (auto* C = Doc->ClassDefinition.Storage.ResolveConstraint(Tok))
        {
            C->Form = ConstraintCategory;
            C->Axis = ComponentClass::ConstraintAxis::AxisY;
        }
    }

    Doc->SelectedToken = Tok;
    MarkActiveDirty();
}

void ComponentWorkspaceSession::DuplicateSelectedComponent()
{
    auto* Doc = AccessActiveDocument();
    if (Doc == nullptr || !Doc->SelectedToken.Valid())
    {
        return;
    }
    const auto Dup = Doc->ClassDefinition.Storage.DuplicateInstance(Doc->SelectedToken);
    if (Dup.Valid())
    {
        Doc->SelectedToken = Dup;
        MarkActiveDirty();
    }
}

void ComponentWorkspaceSession::RemoveSelectedComponent()
{
    auto* Doc = AccessActiveDocument();
    if (Doc == nullptr || !Doc->SelectedToken.Valid() || Doc->SelectedToken == Doc->ClassDefinition.Storage.QueryOriginToken())
    {
        return;
    }
    if (Doc->ClassDefinition.Storage.RemoveInstance(Doc->SelectedToken))
    {
        Doc->SelectedToken = Doc->ClassDefinition.Storage.QueryOriginToken();
        MarkActiveDirty();
    }
}

void ComponentWorkspaceSession::FocusSelectedOrClass()
{
    auto* Doc = AccessActiveDocument();
    if (Doc == nullptr || BoundRenderer == nullptr)
    {
        return;
    }
    Doc->Camera = BoundRenderer->ComputeFramedCameraForClass(
        Doc->ClassDefinition,
        Repository,
        Doc->Camera.YawRadians,
        Doc->Camera.PitchRadians);
    ++RenderMutator;
}

void ComponentWorkspaceSession::ToggleSimulation() noexcept
{
    SimulationActive = !SimulationActive;
    if (!SimulationActive)
    {
        ResetMotionKinematics();
    }
    ++RenderMutator;
}

void ComponentWorkspaceSession::TriggerConstraintKick(ComponentClass::ComponentInstanceToken Token) noexcept
{
    if (Token.Valid())
    {
        KickImpulses[Token.SlotIndex] = 1.0f;
        ++RenderMutator;
    }
}

void ComponentWorkspaceSession::ResetMotionKinematics() noexcept
{
    VehicleTelemetry = SimulationSpace::PhysicsSpace::VehicleSimulationTelemetry{};
    ThrottleInput    = 0.0f;
    BrakeInput       = 0.0f;
    SteerInput       = 0.0f;
    SpringVelocities.clear();
    KickImpulses.clear();
    if (auto* Doc = AccessActiveDocument())
    {
        SyncEditPoseFromClass(*Doc);
    }
    ++RenderMutator;
}

void ComponentWorkspaceSession::SyncEditPoseFromClass(ComponentClassDocument& Doc) noexcept
{
    RenderOptions.LiveConstraintCoordinates.clear();
    RenderOptions.DockIntactMask.clear();
    Doc.ClassDefinition.Storage.WalkSpatialNesting([&](const ComponentClass::ComponentInstanceHeader& H)
    {
        if (H.Category != ComponentClass::ComponentCategory::ConstraintInstance)
        {
            return;
        }
        if (const auto* C = Doc.ClassDefinition.Storage.InspectConstraint(H.Token))
        {
            RenderOptions.LiveConstraintCoordinates[H.Token.SlotIndex] =
                SimulationSpace::PhysicsSpace::VehicleDynamicsSolver::EvaluateConstraintEditScalar(*C);
            RenderOptions.DockIntactMask[H.Token.SlotIndex] = C->DockConnected;
        }
    });
}

void ComponentWorkspaceSession::StepLiveSimulation(double DeltaSeconds) noexcept
{
    auto* Doc = AccessActiveDocument();
    if (Doc == nullptr)
    {
        return;
    }

    const float Dt = static_cast<float>(std::clamp(DeltaSeconds, 0.001, 0.05));
    const ComponentClass::VehicleDynamicsSettings* VehSettings = nullptr;
    std::vector<ComponentClass::ComponentInstanceToken> Behaviours {};
    Doc->ClassDefinition.Storage.CollectBehaviourTokens(Behaviours);
    for (const auto BTok : Behaviours)
    {
        if (const auto* V = Doc->ClassDefinition.Storage.InspectVehicle(BTok))
        {
            VehSettings = V;
            break;
        }
    }

    if (VehSettings != nullptr)
    {
        SimulationSpace::PhysicsSpace::VehicleDynamicsSolver::StepPowertrain(
            *VehSettings, ThrottleInput, BrakeInput, Dt, VehicleTelemetry);
    }

    Doc->ClassDefinition.Storage.WalkSpatialNesting([&](const ComponentClass::ComponentInstanceHeader& H)
    {
        if (H.Category != ComponentClass::ComponentCategory::ConstraintInstance)
        {
            return;
        }
        const auto* C = Doc->ClassDefinition.Storage.InspectConstraint(H.Token);
        if (C == nullptr) return;

        float& Coord = RenderOptions.LiveConstraintCoordinates[H.Token.SlotIndex];
        float& Vel   = SpringVelocities[H.Token.SlotIndex];

        if (C->Form == ComponentClass::PhysicsConstraintCategory::Spring)
        {
            const float Omega = 2.0f * static_cast<float>(Constants::Pi) * std::max(C->NaturalFrequencyHz, 0.25f);
            float Target = 0.0f;
            if (H.Title.find("FL") != std::string::npos || H.Title.find("FR") != std::string::npos)
            {
                Target = -0.015f * BrakeInput + 0.012f * ThrottleInput;
            }
            else
            {
                Target = -0.018f * ThrottleInput + 0.010f * BrakeInput;
            }
            if (auto KickIt = KickImpulses.find(H.Token.SlotIndex); KickIt != KickImpulses.end() && KickIt->second > 0.01f)
            {
                Vel -= 0.75f * KickIt->second;
                KickIt->second = 0.0f;
            }
            const float Acc = Omega * Omega * (Target - Coord) - 2.0f * C->DampingRatioZeta * Omega * Vel;
            Vel   += Acc * Dt;
            Coord  = std::clamp(Coord + Vel * Dt, -C->TravelEachWayMeters, C->TravelEachWayMeters);
        }
        else if (C->Form == ComponentClass::PhysicsConstraintCategory::Hinge)
        {
            if (C->DriveMode == ComponentClass::ConstraintDriveMode::Steer)
            {
                const float MaxRad = std::max(std::abs(C->MinAngleDegrees), std::abs(C->MaxAngleDegrees)) *
                    static_cast<float>(Constants::Pi) / 180.0f;
                Coord = SteerInput * MaxRad;
            }
            else if (C->DriveMode == ComponentClass::ConstraintDriveMode::Motor)
            {
                Coord += (C->TargetRpm * 2.0f * static_cast<float>(Constants::Pi) / 60.0f) * Dt;
            }
        }
        else if (C->Form == ComponentClass::PhysicsConstraintCategory::Spin)
        {
            Coord += (C->TargetRpm * 2.0f * static_cast<float>(Constants::Pi) / 60.0f) * Dt;
        }
        else if (C->Form == ComponentClass::PhysicsConstraintCategory::Dock)
        {
            bool& Intact = RenderOptions.DockIntactMask[H.Token.SlotIndex];
            if (!C->DockConnected)
            {
                Intact = false;
            }
            else if (VehicleTelemetry.AeroDownforceNewtons > C->BreakForceNewtons)
            {
                Intact = false;
            }
        }
    });

    ++RenderMutator;
}

void ComponentWorkspaceSession::Tick(double DeltaSeconds) noexcept
{
    RenderOptions.ElapsedSeconds += static_cast<float>(std::clamp(DeltaSeconds, 0.0, 0.1));
    if (RenderOptions.TurntableEnabled)
    {
        ++RenderMutator;
    }
    if (SimulationActive)
    {
        StepLiveSimulation(DeltaSeconds);
    }
}

uint64_t ComponentWorkspaceSession::ComputeRenderSignature() const noexcept
{
    uint64_t H = 14695981039346656037ull;
    auto Mix = [&](uint64_t V) noexcept
    {
        H ^= V;
        H *= 1099511628211ull;
    };

    Mix(ActiveDocIndex);
    Mix(static_cast<uint64_t>(RenderOptions.Channel));
    Mix(RenderOptions.ShowDotFloor ? 1ull : 0ull);
    Mix(RenderOptions.ShowDebugHelpers ? 1ull : 0ull);
    Mix(RenderOptions.TurntableEnabled ? 1ull : 0ull);
    Mix(RenderOptions.SelectedToken.SlotIndex);
    Mix(RenderMutator);

    if (const auto* Doc = InspectActiveDocument())
    {
        auto MixFloat = [&](float F) noexcept
        {
            uint32_t Bits = 0u;
            std::memcpy(&Bits, &F, sizeof(Bits));
            Mix(Bits);
        };
        Mix(Doc->ClassDefinition.CompiledRevision);
        MixFloat(Doc->Camera.FocusPointMeters.X);
        MixFloat(Doc->Camera.FocusPointMeters.Y);
        MixFloat(Doc->Camera.FocusPointMeters.Z);
        MixFloat(Doc->Camera.YawRadians);
        MixFloat(Doc->Camera.PitchRadians);
        MixFloat(Doc->Camera.DistanceMeters);
        Mix(Doc->Camera.PilotActive ? 1ull : 0ull);
        Mix(Doc->Camera.PilotCameraToken.SlotIndex);
    }
    return H;
}

} // namespace Frontier::Workspaces::ComponentEditor

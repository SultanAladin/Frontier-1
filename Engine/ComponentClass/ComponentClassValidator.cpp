/*============================================================================================================================================
                                                       COMPONENTCLASSVALIDATOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of ComponentClass structural checks (assets, Dock key tags, wheel bindings, torque curve monotonicity)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentClassValidator.h"

namespace Frontier::ComponentClass
{

std::vector<ClassDiagnosticIssue> ComponentClassValidator::ValidateClass(
    const ComponentClassDefinition& Definition,
    const AssetExistsPredicate&     QueryAssetCompiled)
{
    std::vector<ClassDiagnosticIssue> Issues {};
    const ComponentInstanceStorage& Storage = Definition.Storage;
    const uint32_t SlotCount = Storage.QueryRawSlotCount();

    for (uint32_t Idx = 0u; Idx < SlotCount; ++Idx)
    {
        const ComponentInstanceHeader* Header = Storage.InspectHeaderByIndex(Idx);
        if (Header == nullptr)
        {
            continue;
        }

        if (Header->Category == ComponentCategory::GeometryInstance)
        {
            const GeometryComponentSettings* Geom = Storage.InspectGeometry(Header->Token);
            if (Geom == nullptr || Geom->AssetIdentifier.empty())
            {
                Issues.push_back({
                    DiagnosticSeverity::Warning,
                    Header->Token,
                    Header->Title + ": no compiled geometry asset assigned.",
                });
            }
            else if (QueryAssetCompiled && !QueryAssetCompiled(Geom->AssetIdentifier))
            {
                Issues.push_back({
                    DiagnosticSeverity::Error,
                    Header->Token,
                    Header->Title + ": geometry asset '" + Geom->AssetIdentifier + "' is missing or uncompiled.",
                });
            }
        }
        else if (Header->Category == ComponentCategory::ConstraintInstance)
        {
            const ConstraintComponentSettings* Constraint = Storage.InspectConstraint(Header->Token);
            if (Constraint != nullptr && Constraint->Kind == PhysicsConstraintKind::Dock)
            {
                std::vector<ComponentInstanceToken> Nested {};
                Storage.CollectNestedTokens(Header->Token, Nested);
                for (const ComponentInstanceToken ChildTok : Nested)
                {
                    const ComponentInstanceHeader* NestedHeader = Storage.InspectHeader(ChildTok);
                    if (NestedHeader != nullptr &&
                        !ComponentInstanceStorage::QueryDockTagAccepted(Constraint->DockKey, NestedHeader->TagCsv))
                    {
                        Issues.push_back({
                            DiagnosticSeverity::Warning,
                            Header->Token,
                            Header->Title + ": dock key '" + Constraint->DockKey + "' does not accept nested '" + NestedHeader->Title + "'.",
                        });
                    }
                }
            }
        }
        else if (Header->Category == ComponentCategory::VehicleDynamics)
        {
            const VehicleDynamicsSettings* Vehicle = Storage.InspectVehicle(Header->Token);
            if (Vehicle != nullptr)
            {
                if (Vehicle->BoundWheelTokens.size() < 2u)
                {
                    Issues.push_back({
                        DiagnosticSeverity::Error,
                        Header->Token,
                        Header->Title + ": fewer than 2 wheels bound to VehicleDynamics.",
                    });
                }
                for (const ComponentInstanceToken WheelTok : Vehicle->BoundWheelTokens)
                {
                    if (!Storage.AuthenticateToken(WheelTok))
                    {
                        Issues.push_back({
                            DiagnosticSeverity::Error,
                            Header->Token,
                            Header->Title + ": references a removed wheel component.",
                        });
                        break;
                    }
                }

                if (Vehicle->TorqueCurve.size() < 3u)
                {
                    Issues.push_back({
                        DiagnosticSeverity::Error,
                        Header->Token,
                        Header->Title + ": torque curve requires at least 3 samples.",
                    });
                }
                else
                {
                    for (std::size_t K = 1u; K < Vehicle->TorqueCurve.size(); ++K)
                    {
                        if (Vehicle->TorqueCurve[K].Rpm <= Vehicle->TorqueCurve[K - 1u].Rpm)
                        {
                            Issues.push_back({
                                DiagnosticSeverity::Error,
                                Header->Token,
                                Header->Title + ": torque curve RPM samples must strictly increase.",
                            });
                            break;
                        }
                    }
                }
            }
        }
        else if (Header->Category == ComponentCategory::RotationalMotion)
        {
            const RotationalMotionSettings* Rot = Storage.InspectRotation(Header->Token);
            if (Rot != nullptr && Rot->TargetInstance.Valid() && !Storage.AuthenticateToken(Rot->TargetInstance))
            {
                Issues.push_back({
                    DiagnosticSeverity::Warning,
                    Header->Token,
                    Header->Title + ": target instance no longer exists.",
                });
            }
        }
    }

    return Issues;
}

bool ComponentClassValidator::QueryHasBlockingErrors(const std::vector<ClassDiagnosticIssue>& Issues) noexcept
{
    for (const ClassDiagnosticIssue& Issue : Issues)
    {
        if (Issue.Severity == DiagnosticSeverity::Error)
        {
            return true;
        }
    }
    return false;
}

} // namespace Frontier::ComponentClass

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
    const AssetExistsCondition&     QueryAssetCompiled)
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
            if (Constraint != nullptr && Constraint->Form == PhysicsConstraintCategory::Dock)
            {
                std::vector<ComponentInstanceToken> Nested {};
                Storage.CollectNestedTokens(Header->Token, Nested);
                for (const ComponentInstanceToken NestedTok : Nested)
                {
                    const ComponentInstanceHeader* NestedHeader = Storage.InspectHeader(NestedTok);
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
        else if (Header->Category == ComponentCategory::RigidBodyInstance)
        {
            const RigidBodyComponentSettings* Rb = Storage.InspectRigidBody(Header->Token);
            if (Rb != nullptr && Rb->MotionKind == RigidBodyMotionKind::Dynamic
                && Rb->ExplicitMassKg <= 0.0f && Rb->DensityKgPerM3 <= 0.0f)
            {
                Issues.push_back({
                    DiagnosticSeverity::Error,
                    Header->Token,
                    Header->Title + ": dynamic rigid body requires positive ExplicitMassKg or DensityKgPerM3.",
                });
            }
        }
        else if (Header->Category == ComponentCategory::SoftBodyInstance)
        {
            const SoftBodyComponentSettings* Sb = Storage.InspectSoftBody(Header->Token);
            if (Sb == nullptr || Sb->SoftBodyAssetPath.empty())
            {
                Issues.push_back({
                    DiagnosticSeverity::Error,
                    Header->Token,
                    Header->Title + ": SoftBodyInstance requires a non-empty .SoftBody asset path.",
                });
            }
            else
            {
                if (QueryAssetCompiled && !QueryAssetCompiled(Sb->SoftBodyAssetPath))
                {
                    Issues.push_back({
                        DiagnosticSeverity::Error,
                        Header->Token,
                        Header->Title + ": .SoftBody asset '" + Sb->SoftBodyAssetPath + "' is missing.",
                    });
                }
                if (!Sb->VertexAtlasPath.empty() && QueryAssetCompiled && !QueryAssetCompiled(Sb->VertexAtlasPath))
                {
                    Issues.push_back({
                        DiagnosticSeverity::Error,
                        Header->Token,
                        Header->Title + ": .VertexAtlas asset '" + Sb->VertexAtlasPath + "' is missing.",
                    });
                }
                if (Sb->SleepDistanceMeters <= 0.0f)
                {
                    Issues.push_back({
                        DiagnosticSeverity::Warning,
                        Header->Token,
                        Header->Title + ": SleepDistanceMeters should be positive.",
                    });
                }
            }
        }
        else if (Header->Category == ComponentCategory::ForceFieldInstance)
        {
            const ForceFieldComponentSettings* Ff = Storage.InspectForceField(Header->Token);
            if (Ff != nullptr)
            {
                if (Ff->RadiusMeters <= 0.0f)
                {
                    Issues.push_back({
                        DiagnosticSeverity::Error,
                        Header->Token,
                        Header->Title + ": ForceFieldInstance requires positive RadiusMeters.",
                    });
                }
                const float dirSq = Ff->Direction.X * Ff->Direction.X + Ff->Direction.Y * Ff->Direction.Y + Ff->Direction.Z * Ff->Direction.Z;
                if (Ff->ModelCode == 0u && dirSq <= 1e-6f)
                {
                    Issues.push_back({
                        DiagnosticSeverity::Warning,
                        Header->Token,
                        Header->Title + ": directional wind force field has zero direction vector.",
                    });
                }
            }
        }
        else if (Header->Category == ComponentCategory::FluidRegionInstance)
        {
            const FluidRegionComponentSettings* Fl = Storage.InspectFluidRegion(Header->Token);
            if (Fl != nullptr)
            {
                if (Fl->RadiusMeters <= 0.0f)
                {
                    Issues.push_back({
                        DiagnosticSeverity::Error,
                        Header->Token,
                        Header->Title + ": FluidRegionInstance requires positive RadiusMeters.",
                    });
                }
                if (Fl->Density <= 0.0f && Fl->Temperature <= 0.0f && Fl->Fuel <= 0.0f)
                {
                    Issues.push_back({
                        DiagnosticSeverity::Warning,
                        Header->Token,
                        Header->Title + ": fluid source injects zero density, temperature, and fuel.",
                    });
                }
            }
        }
        else if (Header->Category == ComponentCategory::ParticleCloudInstance)
        {
            const ParticleCloudComponentSettings* Pc = Storage.InspectParticleCloud(Header->Token);
            if (Pc != nullptr)
            {
                if (Pc->ParticleCount == 0u || Pc->LifetimeSec <= 0.0f || Pc->RadiusMeters <= 0.0f)
                {
                    Issues.push_back({
                        DiagnosticSeverity::Error,
                        Header->Token,
                        Header->Title + ": ParticleCloudInstance requires positive ParticleCount, LifetimeSec, and RadiusMeters.",
                    });
                }
                if (Pc->ReleaseRate <= 0.0f)
                {
                    Issues.push_back({
                        DiagnosticSeverity::Warning,
                        Header->Token,
                        Header->Title + ": ParticleCloudInstance has zero ReleaseRate.",
                    });
                }
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

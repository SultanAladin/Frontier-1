/*============================================================================================================================================
                                                             PHYSICSMATERIAL.H
============================================================================================================================================*/
// 🧩 Surface physics material descriptors, combine policies, and O(1) lock-free material register for rigid bodies and tire contacts

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace Frontier::SimulationSpace::PhysicsSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint16_t MaxRegisteredPhysicsMaterials = 64u; // [cnt] - Fixed zero-allocation register capacity
inline constexpr uint16_t DefaultAsphaltMaterialId      = 0u;  // [id]  - Built-in dry asphalt default
inline constexpr uint16_t DefaultConcreteMaterialId     = 1u;  // [id]  - Built-in brushed concrete
inline constexpr uint16_t DefaultWetAsphaltMaterialId   = 2u;  // [id]  - Built-in wet asphalt
inline constexpr uint16_t DefaultGravelMaterialId       = 3u;  // [id]  - Built-in loose gravel
inline constexpr uint16_t DefaultGrassMaterialId        = 4u;  // [id]  - Built-in short grass / verge
inline constexpr uint16_t DefaultCurbMaterialId         = 5u;  // [id]  - Built-in painted track curb
inline constexpr uint16_t DefaultIceMaterialId          = 6u;  // [id]  - Built-in packed ice

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class SurfaceMaterialCategory : uint8_t
{
    Asphalt    = 0u, // [-] - Dry tarmac / race asphalt
    Concrete   = 1u, // [-] - Brushed highway concrete
    WetAsphalt = 2u, // [-] - Rain-slick tarmac
    Gravel     = 3u, // [-] - Loose rally gravel
    Grass      = 4u, // [-] - Turf / off-track verge
    Curb       = 5u, // [-] - Painted FIA track kerb
    Ice        = 6u, // [-] - Low-mu ice sheet
    Metal      = 7u, // [-] - Structural steel / aluminum hull
    Rubber     = 8u, // [-] - Vulcanized elastomer
    Custom     = 9u, // [-] - Author-defined material
};

enum class SurfaceCombinePolicy : uint8_t
{
    Average  = 0u, // [-] - Arithmetic mean (0.5 * (A + B))
    Minimum  = 1u, // [-] - Lower of the two surfaces
    Maximum  = 2u, // [-] - Higher of the two surfaces
    Multiply = 3u, // [-] - Product (A * B)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PhysicsMaterialSpecification
{
    uint16_t                MaterialId         = DefaultAsphaltMaterialId;     // [id]     - Register slot identifier
    SurfaceMaterialCategory Category           = SurfaceMaterialCategory::Asphalt; // [-]  - Semantic surface class
    SurfaceCombinePolicy    FrictionCombine    = SurfaceCombinePolicy::Average;// [-]      - Rigid contact friction combine rule
    SurfaceCombinePolicy    RestitutionCombine = SurfaceCombinePolicy::Maximum;// [-]      - Rigid contact bounce combine rule
    float                   StaticFriction     = 0.85f;                        // [-]      - Rigid body static Coulomb friction
    float                   DynamicFriction    = 0.72f;                        // [-]      - Rigid body kinetic friction
    float                   Restitution        = 0.08f;                        // [0-1]    - Elastic bounce restitution
    float                   DensityKgPerM3     = 2350.0f;                      // [kg/m^3] - Bulk material density
    float                   TireGripScale      = 1.00f;                        // [-]      - Pacejka MF6.1 grip multiplier
    float                   RollingResistScale = 1.00f;                        // [-]      - Tire rolling resistance multiplier
    float                   PeakSlipShift      = 1.00f;                        // [-]      - Peak slip ratio shift (>1 on gravel/ice)
    char                    Label[32]          { "Asphalt" };                  // [-]      - Fixed ASCII label (zero heap allocation)

    void AssignLabel(std::string_view Text) noexcept
    {
        const std::size_t CopyLen = (Text.size() < sizeof(Label) - 1u) ? Text.size() : (sizeof(Label) - 1u);
        std::memcpy(Label, Text.data(), CopyLen);
        Label[CopyLen] = '\0';
    }

    [[nodiscard]] static PhysicsMaterialSpecification ConstructPreset(SurfaceMaterialCategory Surface) noexcept
    {
        PhysicsMaterialSpecification Mat {};
        Mat.Category = Surface;
        switch (Surface)
        {
            case SurfaceMaterialCategory::Asphalt:
                Mat.MaterialId         = DefaultAsphaltMaterialId;
                Mat.StaticFriction     = 0.90f;
                Mat.DynamicFriction    = 0.78f;
                Mat.Restitution        = 0.05f;
                Mat.TireGripScale      = 1.00f;
                Mat.RollingResistScale = 1.00f;
                Mat.PeakSlipShift      = 1.00f;
                Mat.AssignLabel("Asphalt");
                break;

            case SurfaceMaterialCategory::Concrete:
                Mat.MaterialId         = DefaultConcreteMaterialId;
                Mat.StaticFriction     = 0.86f;
                Mat.DynamicFriction    = 0.74f;
                Mat.Restitution        = 0.08f;
                Mat.TireGripScale      = 0.96f;
                Mat.RollingResistScale = 1.02f;
                Mat.PeakSlipShift      = 1.00f;
                Mat.AssignLabel("Concrete");
                break;

            case SurfaceMaterialCategory::WetAsphalt:
                Mat.MaterialId         = DefaultWetAsphaltMaterialId;
                Mat.StaticFriction     = 0.62f;
                Mat.DynamicFriction    = 0.50f;
                Mat.Restitution        = 0.04f;
                Mat.TireGripScale      = 0.70f;
                Mat.RollingResistScale = 1.15f;
                Mat.PeakSlipShift      = 0.88f;
                Mat.AssignLabel("WetAsphalt");
                break;

            case SurfaceMaterialCategory::Gravel:
                Mat.MaterialId         = DefaultGravelMaterialId;
                Mat.StaticFriction     = 0.58f;
                Mat.DynamicFriction    = 0.48f;
                Mat.Restitution        = 0.02f;
                Mat.TireGripScale      = 0.62f;
                Mat.RollingResistScale = 2.40f;
                Mat.PeakSlipShift      = 1.45f;
                Mat.AssignLabel("Gravel");
                break;

            case SurfaceMaterialCategory::Grass:
                Mat.MaterialId         = DefaultGrassMaterialId;
                Mat.StaticFriction     = 0.52f;
                Mat.DynamicFriction    = 0.42f;
                Mat.Restitution        = 0.02f;
                Mat.TireGripScale      = 0.55f;
                Mat.RollingResistScale = 2.10f;
                Mat.PeakSlipShift      = 1.25f;
                Mat.AssignLabel("Grass");
                break;

            case SurfaceMaterialCategory::Curb:
                Mat.MaterialId         = DefaultCurbMaterialId;
                Mat.StaticFriction     = 0.82f;
                Mat.DynamicFriction    = 0.68f;
                Mat.Restitution        = 0.10f;
                Mat.TireGripScale      = 0.90f;
                Mat.RollingResistScale = 1.12f;
                Mat.PeakSlipShift      = 0.96f;
                Mat.AssignLabel("Curb");
                break;

            case SurfaceMaterialCategory::Ice:
                Mat.MaterialId         = DefaultIceMaterialId;
                Mat.StaticFriction     = 0.22f;
                Mat.DynamicFriction    = 0.14f;
                Mat.Restitution        = 0.06f;
                Mat.TireGripScale      = 0.24f;
                Mat.RollingResistScale = 0.85f;
                Mat.PeakSlipShift      = 0.65f;
                Mat.AssignLabel("Ice");
                break;

            case SurfaceMaterialCategory::Metal:
                Mat.StaticFriction     = 0.55f;
                Mat.DynamicFriction    = 0.42f;
                Mat.Restitution        = 0.22f;
                Mat.DensityKgPerM3     = 7850.0f;
                Mat.TireGripScale      = 0.68f;
                Mat.RollingResistScale = 0.95f;
                Mat.AssignLabel("Metal");
                break;

            case SurfaceMaterialCategory::Rubber:
                Mat.StaticFriction     = 1.05f;
                Mat.DynamicFriction    = 0.88f;
                Mat.Restitution        = 0.35f;
                Mat.DensityKgPerM3     = 1150.0f;
                Mat.TireGripScale      = 1.08f;
                Mat.RollingResistScale = 1.05f;
                Mat.AssignLabel("Rubber");
                break;

            case SurfaceMaterialCategory::Custom:
                Mat.AssignLabel("Custom");
                break;
        }
        return Mat;
    }
};

class PhysicsMaterialRegister
{
public:
    PhysicsMaterialRegister() noexcept
    {
        ResetToBuiltInDefaults();
    }

    void ResetToBuiltInDefaults() noexcept
    {
        for (uint16_t Idx = 0u; Idx < MaxRegisteredPhysicsMaterials; ++Idx)
        {
            Slots[Idx]            = PhysicsMaterialSpecification::ConstructPreset(SurfaceMaterialCategory::Asphalt);
            Slots[Idx].MaterialId = Idx;
            SlotOccupied[Idx]     = false;
        }
        RegisterBuiltIn(DefaultAsphaltMaterialId,    SurfaceMaterialCategory::Asphalt);
        RegisterBuiltIn(DefaultConcreteMaterialId,   SurfaceMaterialCategory::Concrete);
        RegisterBuiltIn(DefaultWetAsphaltMaterialId, SurfaceMaterialCategory::WetAsphalt);
        RegisterBuiltIn(DefaultGravelMaterialId,     SurfaceMaterialCategory::Gravel);
        RegisterBuiltIn(DefaultGrassMaterialId,      SurfaceMaterialCategory::Grass);
        RegisterBuiltIn(DefaultCurbMaterialId,       SurfaceMaterialCategory::Curb);
        RegisterBuiltIn(DefaultIceMaterialId,        SurfaceMaterialCategory::Ice);
        NextCustomSlot = 7u;
    }

    [[nodiscard]] uint16_t RegisterMaterial(const PhysicsMaterialSpecification& Spec) noexcept
    {
        uint16_t TargetId = Spec.MaterialId;
        if (TargetId >= MaxRegisteredPhysicsMaterials)
        {
            TargetId = (NextCustomSlot < MaxRegisteredPhysicsMaterials) ? NextCustomSlot++ : (MaxRegisteredPhysicsMaterials - 1u);
        }
        else if (TargetId >= NextCustomSlot)
        {
            NextCustomSlot = static_cast<uint16_t>(TargetId + 1u);
        }
        Slots[TargetId]            = Spec;
        Slots[TargetId].MaterialId = TargetId;
        SlotOccupied[TargetId]     = true;
        return TargetId;
    }

    [[nodiscard]] const PhysicsMaterialSpecification& InspectMaterial(uint16_t MaterialId) const noexcept
    {
        if (MaterialId < MaxRegisteredPhysicsMaterials && SlotOccupied[MaterialId])
        {
            return Slots[MaterialId];
        }
        return Slots[DefaultAsphaltMaterialId];
    }

    [[nodiscard]] uint16_t QueryRegisteredCount() const noexcept
    {
        uint16_t Count = 0u;
        for (bool Used : SlotOccupied)
        {
            if (Used)
            {
                ++Count;
            }
        }
        return Count;
    }

private:
    void RegisterBuiltIn(uint16_t Id, SurfaceMaterialCategory Category) noexcept
    {
        Slots[Id]            = PhysicsMaterialSpecification::ConstructPreset(Category);
        Slots[Id].MaterialId = Id;
        SlotOccupied[Id]     = true;
    }

    std::array<PhysicsMaterialSpecification, MaxRegisteredPhysicsMaterials> Slots        {};
    std::array<bool, MaxRegisteredPhysicsMaterials>                         SlotOccupied {};
    uint16_t                                                                NextCustomSlot = 7u;
};

} // namespace Frontier::SimulationSpace::PhysicsSpace

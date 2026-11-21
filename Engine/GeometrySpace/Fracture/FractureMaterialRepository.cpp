/*============================================================================================================================================
                                                     FRACTUREMATERIALREPOSITORY.CPP
============================================================================================================================================*/
// 🧩 Physical SI fracture material repository and deterministic Mulberry32 PRNG stream (RHR +Z Up)

#include "Engine/GeometrySpace/Fracture/FractureMaterialRepository.h"

#include <cmath>
#include <cstring>

namespace Frontier::GeometrySpace::Fracture
{

namespace
{

constexpr FractureMaterialSpec MaterialEntries[] = {
    {
        "concrete", "Reinforced Concrete", "Concrete",
        FractureMaterialCategory::BulkBrittle,
        2350.0f, 140.0f, 30.0f, 0.20f, 3.8f, 8.0f, 0.0f,
        0.95f, 1.15f, { 0.0f, 0.0f, 1.0f }, 0.12f, 0.24f, 0.32f,
        0.12f, 0.80f, 2.20f,
        { 0.266f, 0.278f, 0.258f }, { 0.159f, 0.165f, 0.147f },
        0.92f, 0.02f, 0.0f
    },
    {
        "granite", "Bedded Granite", "Granite",
        FractureMaterialCategory::BulkBrittle,
        2700.0f, 95.0f, 55.0f, 0.25f, 12.5f, 9.0f, 0.0f,
        1.02f, 1.40f, { 0.0f, 0.0f, 1.0f }, 0.55f, 0.18f, 0.28f,
        0.10f, 0.90f, 1.60f,
        { 0.159f, 0.171f, 0.188f }, { 0.238f, 0.212f, 0.188f },
        0.88f, 0.05f, 0.0f
    },
    {
        "wood", "Orthotropic Spruce", "Spruce Wood",
        FractureMaterialCategory::Fibrous,
        520.0f, 320.0f, 11.0f, 0.35f, 78.0f, 6.0f, 0.0f,
        0.0f, 11.0f, { 1.0f, 0.0f, 0.0f }, 0.0f, 0.06f, 0.14f,
        0.22f, 0.58f, 0.25f,
        { 0.451f, 0.223f, 0.072f }, { 0.665f, 0.439f, 0.195f },
        0.76f, 0.0f, 0.0f
    },
    {
        "glass", "Annealed Float Glass", "Float Glass",
        FractureMaterialCategory::PlateBrittle,
        2500.0f, 7.0f, 70.0f, 0.22f, 45.0f, 5.0f, 0.0f,
        1.15f, 1.0f, { 0.0f, 0.0f, 1.0f }, 0.0f, 0.44f, 0.18f,
        0.16f, 0.28f, 0.75f,
        { 0.479f, 0.716f, 0.776f }, { 0.738f, 0.905f, 0.939f },
        0.06f, 0.05f, 0.82f
    },
    {
        "tempered", "Tempered Safety Glass", "Tempered",
        FractureMaterialCategory::PlateBrittle,
        2500.0f, 7.0f, 70.0f, 0.22f, 170.0f, 12.0f, 46000.0f,
        0.0f, 1.0f, { 0.0f, 0.0f, 1.0f }, 0.0f, 0.52f, 0.72f,
        0.14f, 0.35f, 1.45f,
        { 0.392f, 0.687f, 0.791f }, { 0.672f, 0.888f, 0.956f },
        0.08f, 0.06f, 0.78f
    },
    {
        "plastic", "Ductile ABS Polymer", "ABS Polymer",
        FractureMaterialCategory::Ductile,
        1050.0f, 460.0f, 2.2f, 0.35f, 42.0f, 20.0f, 0.0f,
        0.0f, 1.0f, { 0.0f, 0.0f, 1.0f }, 0.0f, 0.12f, 0.48f,
        0.36f, 0.45f, 0.05f,
        { 0.694f, 0.105f, 0.039f }, { 0.871f, 0.456f, 0.342f },
        0.38f, 0.02f, 0.0f
    }
};

constexpr EngineeringConstitutiveSpec EngineeringEntries[] = {
    { 38.0f,  36.0f, 0.78f,  6.5f, 0.58f, 0.036f, "Bouchaud H=0.78 + Meso-Aggregate Pullout Sockets" },
    { 165.0f, 48.0f, 0.80f,  9.5f, 0.44f, 0.030f, "Crystalline Cleavage Steps + Quartz Clast Deflection" },
    { 45.0f,  24.0f, 0.72f, 14.0f, 0.00f, 0.042f, "12:1 Orthotropic Tracheid Fiber Splinter Flutes" },
    { 700.0f, 30.0f, 0.55f, 12.0f, 0.00f, 0.014f, "Mirror-Mist-Hackle Zones + Wallner Sonic Ripples" },
    { 900.0f, 32.0f, 0.58f, 16.0f, 0.00f, 0.010f, "Bifurcated Residual-Core Dicing + Wallner Steps" },
    { 68.0f,   8.0f, 0.66f,  8.0f, 0.00f, 0.032f, "Cup-and-Cone Perimeter Shear Lip + Void Dimples" }
};

} // namespace

DeterministicRandomStream::DeterministicRandomStream(uint32_t Seed) noexcept
{
    uint32_t SafeSeed = Seed != 0u ? Seed : 42u;
    Register = SafeSeed * 2654435761u;
    if (Register == 0u) Register = 1u;
}

double DeterministicRandomStream::NextDouble() noexcept
{
    Register += 0x6D2B79F5u;
    uint32_t T = Register;
    T = (T ^ (T >> 15u)) * (T | 1u);
    T ^= T + (T ^ (T >> 7u)) * (T | 61u);
    return static_cast<double>(T ^ (T >> 14u)) / 4294967296.0;
}

float DeterministicRandomStream::NextFloat() noexcept
{
    return static_cast<float>(NextDouble());
}

const FractureMaterialSpec& FractureMaterialRepository::LookupMaterial(const char* Identifier) noexcept
{
    if (Identifier != nullptr)
    {
        for (const auto& Entry : MaterialEntries)
        {
            if (std::strcmp(Entry.Identifier, Identifier) == 0)
            {
                return Entry;
            }
        }
    }
    return MaterialEntries[0];
}

const FractureMaterialSpec& FractureMaterialRepository::LookupMaterialByIndex(uint32_t Index) noexcept
{
    constexpr uint32_t Count = sizeof(MaterialEntries) / sizeof(MaterialEntries[0]);
    return MaterialEntries[Index < Count ? Index : 0u];
}

uint32_t FractureMaterialRepository::QueryMaterialCount() noexcept
{
    return static_cast<uint32_t>(sizeof(MaterialEntries) / sizeof(MaterialEntries[0]));
}

const EngineeringConstitutiveSpec& FractureMaterialRepository::LookupEngineeringSpec(const char* Identifier) noexcept
{
    constexpr uint32_t Count = sizeof(MaterialEntries) / sizeof(MaterialEntries[0]);
    if (Identifier != nullptr)
    {
        for (uint32_t Index = 0u; Index < Count; ++Index)
        {
            if (std::strcmp(MaterialEntries[Index].Identifier, Identifier) == 0)
            {
                return EngineeringEntries[Index];
            }
        }
    }
    return EngineeringEntries[0];
}

double FractureMaterialRepository::ComputeRayleighWaveSpeedMps(const FractureMaterialSpec& Spec) noexcept
{
    const double ShearModulusPa = (static_cast<double>(Spec.YoungModulusGPa) * 1.0e9)
        / (2.0 * (1.0 + static_cast<double>(Spec.PoissonRatio)));
    return 0.92 * std::sqrt(ShearModulusPa / static_cast<double>(Spec.DensityKgPerM3));
}

} // namespace Frontier::GeometrySpace::Fracture

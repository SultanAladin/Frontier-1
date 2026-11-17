/*============================================================================================================================================
                                                         FLUIDGILIGHTEXTRACTOR.CPP
============================================================================================================================================*/
// 🧩 Clusters high-temperature blackbody cells across 2x2x2 octants into bounded GI emissive light proxies

#include "Engine/SimulationSpace/FluidSpace/FluidGiLightExtractor.h"
#include "Engine/SimulationSpace/FluidSpace/FluidPresets.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::SimulationSpace::FluidSpace
{

void FluidGiLightExtractor::ExtractEmissiveLights(
    const FluidGridVolume&               HeroGrid,
    const FluidShadingSettings&          Shading,
    const FluidGiCouplingSettings&       GiCoupling,
    std::vector<FluidEmissiveLightProxy>& OutLights)
{
    OutLights.clear();
    if (!GiCoupling.EmitGiLights || GiCoupling.MaxEmissiveProxyLights == 0u || HeroGrid.Resolution == 0u)
    {
        return;
    }

    const uint32_t R    = HeroGrid.Resolution;
    const uint32_t Rd   = HeroGrid.ReadIdx();
    const float    FR   = static_cast<float>(R);
    const Mathematics::Axis3D Span = HeroGrid.BoxMaxWorld - HeroGrid.BoxMinWorld;
    const float CellVol = (Span.X / FR) * (Span.Y / FR) * (Span.Z / FR);

    struct OctantAccum
    {
        double SumW   = 0.0;
        double SumX   = 0.0;
        double SumY   = 0.0;
        double SumZ   = 0.0;
        double SumR   = 0.0;
        double SumG   = 0.0;
        double SumB   = 0.0;
        float  PeakT  = 0.0f;
    };
    std::array<OctantAccum, 8> Octants {};

    const uint32_t Stride = (R >= 64u) ? 2u : 1u;
    const float    StrideWeight = static_cast<float>(Stride * Stride * Stride);

    for (uint32_t IZ = 0u; IZ < R; IZ += Stride)
    {
        const float WZ = HeroGrid.BoxMinWorld.Z + ((static_cast<float>(IZ) + 0.5f) / FR) * Span.Z;
        const uint32_t OZ = (IZ * 2u >= R) ? 1u : 0u;
        for (uint32_t IY = 0u; IY < R; IY += Stride)
        {
            const float WY = HeroGrid.BoxMinWorld.Y + ((static_cast<float>(IY) + 0.5f) / FR) * Span.Y;
            const uint32_t OY = (IY * 2u >= R) ? 1u : 0u;
            for (uint32_t IX = 0u; IX < R; IX += Stride)
            {
                const uint32_t Idx = FluidGridVolume::FlatIndex(
                    static_cast<int32_t>(IX), static_cast<int32_t>(IY), static_cast<int32_t>(IZ), R);
                const float Temp = HeroGrid.Thermo[Rd][Idx].Y;
                if (Temp <= 0.35f) continue;

                const auto Em = FluidPresets::EvaluateBlackbodyPalette(Temp, Shading.TemperatureScale, Shading.ColorPalette);
                const float Lum = 0.2126f * Em.R + 0.7152f * Em.G + 0.0722f * Em.B;
                if (Lum <= 0.02f) continue;

                const float WX = HeroGrid.BoxMinWorld.X + ((static_cast<float>(IX) + 0.5f) / FR) * Span.X;
                const uint32_t OX = (IX * 2u >= R) ? 1u : 0u;
                const uint32_t Oct = OX | (OY << 1u) | (OZ << 2u);

                const double W = static_cast<double>(Lum * CellVol * StrideWeight);
                auto& O = Octants[Oct];
                O.SumW += W;
                O.SumX += static_cast<double>(WX) * W;
                O.SumY += static_cast<double>(WY) * W;
                O.SumZ += static_cast<double>(WZ) * W;
                O.SumR += static_cast<double>(Em.R) * static_cast<double>(CellVol * StrideWeight);
                O.SumG += static_cast<double>(Em.G) * static_cast<double>(CellVol * StrideWeight);
                O.SumB += static_cast<double>(Em.B) * static_cast<double>(CellVol * StrideWeight);
                O.PeakT = std::max(O.PeakT, Temp);
            }
        }
    }

    std::vector<FluidEmissiveLightProxy> Candidates;
    for (const auto& O : Octants)
    {
        if (O.SumW <= 1.0e-5) continue;
        const float Gain = GiCoupling.EmissiveLightIntensity * Shading.FireIntensity * 0.38f;
        FluidEmissiveLightProxy L {};
        L.WorldPosition = {
            static_cast<float>(O.SumX / O.SumW),
            static_cast<float>(O.SumY / O.SumW),
            static_cast<float>(O.SumZ / O.SumW),
        };
        L.RadiusMeters = std::clamp(0.45f * std::cbrt(static_cast<float>(O.SumW + 0.1)), 0.35f, 3.5f);
        L.ColorIrradiance = {
            static_cast<float>(O.SumR) * Gain,
            static_cast<float>(O.SumG) * Gain,
            static_cast<float>(O.SumB) * Gain,
        };
        Candidates.push_back(L);
    }

    std::sort(Candidates.begin(), Candidates.end(), [](const FluidEmissiveLightProxy& A, const FluidEmissiveLightProxy& B)
    {
        const float LA = A.ColorIrradiance.R + A.ColorIrradiance.G + A.ColorIrradiance.B;
        const float LB = B.ColorIrradiance.R + B.ColorIrradiance.G + B.ColorIrradiance.B;
        return LA > LB;
    });

    const uint32_t Limit = std::min(MaxProxyLights, GiCoupling.MaxEmissiveProxyLights);
    if (Candidates.size() > Limit) Candidates.resize(Limit);
    OutLights = std::move(Candidates);
}

} // namespace Frontier::SimulationSpace::FluidSpace

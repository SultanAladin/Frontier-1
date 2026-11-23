/*==========================================================================================================================================
                                           PLUMELATTICEFOOTPRINTVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Validate demo-compatible plume footprints and echo accepted bytes to the GPU fixture.
#include "Engine/Rendering/PlumeLatticeFootprint.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace Frontier;

PlumeLatticeFootprint ConstructValid()
{
    PlumeLatticeFootprint Value;
    Value.VelocityExtent = {8, 8, 8, 0};
    Value.ThermoExtent = {16, 16, 16, 0};
    Value.Lattice = {.5f, 1.0f, 0.0f, 1.0f / 30.0f};
    Value.Retention = {1, 1, 1, 1};
    Value.SourceMinimum[3] = .1f;
    Value.Optical = {.5f, .9f, 0, 8};
    Value.Sun = {0, 0, 1, .1f};
    Value.Combustion[2] = 1;
    return Value;
}

template<class Action> void RequireRejection(Action Check)
{
    try { Check(); }
    catch (const std::invalid_argument &) { return; }
    throw std::runtime_error("An invalid plume footprint was accepted");
}

void ExerciseGuards()
{
    const auto Valid = ConstructValid();
    ValidatePlumeLatticeFootprint(Valid, 1);
    auto Bad = Valid; Bad.VelocityExtent[0] = 0;
    RequireRejection([&] { ValidatePlumeLatticeFootprint(Bad, 1); });
    Bad = Valid; Bad.ThermoExtent[1] = 15;
    RequireRejection([&] { ValidatePlumeLatticeFootprint(Bad, 1); });
    Bad = Valid; Bad.VelocityExtent[0] = 257;
    RequireRejection([&] { ValidatePlumeLatticeFootprint(Bad, 1); });
    Bad = Valid; Bad.VelocityOrigin[0] = std::numeric_limits<std::int32_t>::max();
    RequireRejection([&] { ValidatePlumeLatticeFootprint(Bad, 1); });
    Bad = Valid; Bad.PriorVelocityOrigin[3] = 2;
    RequireRejection([&] { ValidatePlumeLatticeFootprint(Bad, 1); });
    Bad = Valid; Bad.PriorVelocityOrigin[3] = -1;
    RequireRejection([&] { ValidatePlumeLatticeFootprint(Bad, 24); });
    Bad = Valid; Bad.Lattice[1] = 1.1f;
    RequireRejection([&] { ValidatePlumeLatticeFootprint(Bad, 1); });
    Bad = Valid; Bad.Lattice[0] = 1e-8f; Bad.Lattice[1] = 0;
    RequireRejection([&] { ValidatePlumeLatticeFootprint(Bad, 1); });
    Bad = Valid; Bad.Lattice[3] = .04f;
    RequireRejection([&] { ValidatePlumeLatticeFootprint(Bad, 1); });
    Bad = Valid; Bad.Wind[0] = std::numeric_limits<float>::quiet_NaN();
    RequireRejection([&] { ValidatePlumeLatticeFootprint(Bad, 1); });
    Bad = Valid; Bad.Retention[0] = 1.01f;
    RequireRejection([&] { ValidatePlumeLatticeFootprint(Bad, 1); });
    Bad = Valid; Bad.Sun[2] = .5f;
    RequireRejection([&] { ValidatePlumeLatticeFootprint(Bad, 1); });
    Bad = Valid; Bad.PriorVelocityOrigin[3] = 1; Bad.SourceMinimum[0] = 1; Bad.SourceMaximum[0] = 0;
    RequireRejection([&] { ValidatePlumeLatticeFootprint(Bad, 1); });
    PlumeSourceFootprint Source;
    Source.PositionRadius = {0, 0, 0, .5f};
    ValidatePlumeSourceFootprints(&Source, 1);
    Source.PositionRadius[3] = 0;
    RequireRejection([&] { ValidatePlumeSourceFootprints(&Source, 1); });
    Source.PositionRadius[3] = .5f; Source.Rates[2] = -1;
    RequireRejection([&] { ValidatePlumeSourceFootprints(&Source, 1); });
    Source.Rates[2] = 0; Source.Velocity[0] = 1e10f;
    RequireRejection([&] { ValidatePlumeSourceFootprints(&Source, 1); });
    RequireRejection([&] { ValidatePlumeSourceFootprints(nullptr, 1); });
    ValidatePlumeSourceFootprints(nullptr, 0);
    std::printf("PLUME_FOOTPRINT_GUARDS rejected=17 valid_lattice=1 valid_source=1 empty_sources=1\n");
}

int Run(int Count, char **Arguments)
{
    if (Count != 2) throw std::runtime_error("Expected --guards, --lattice or --sources");
    const std::string Mode = Arguments[1];
    if (Mode == "--guards") { ExerciseGuards(); return 0; }
    if (Mode == "--lattice")
    {
        PlumeLatticeFootprint Footprint;
        if (std::fread(&Footprint, 1, sizeof(Footprint), stdin) != sizeof(Footprint) || std::fgetc(stdin) != EOF)
            throw std::runtime_error("Expected exactly one lattice footprint");
        ValidatePlumeLatticeFootprint(Footprint, 1);
        if (std::fwrite(&Footprint, 1, sizeof(Footprint), stdout) != sizeof(Footprint))
            throw std::runtime_error("Lattice footprint output failed");
        return 0;
    }
    if (Mode == "--sources")
    {
        std::vector<PlumeSourceFootprint> Sources;
        PlumeSourceFootprint Source;
        for (;;)
        {
            const auto Bytes = std::fread(&Source, 1, sizeof(Source), stdin);
            if (Bytes == 0 && std::feof(stdin)) break;
            if (Bytes != sizeof(Source)) throw std::runtime_error("Source input has an incomplete footprint");
            if (Sources.size() == 24) throw std::runtime_error("Source input exceeds the bounded capacity");
            Sources.push_back(Source);
        }
        ValidatePlumeSourceFootprints(Sources.data(), Sources.size());
        if (!Sources.empty() && std::fwrite(Sources.data(), sizeof(Source), Sources.size(), stdout) != Sources.size())
            throw std::runtime_error("Source footprint output failed");
        return 0;
    }
    throw std::runtime_error("Unknown validation mode");
}
} // namespace

int main(int Count, char **Arguments)
{
    try { return Run(Count, Arguments); }
    catch (const std::exception &Error) { std::fprintf(stderr, "FAIL %s\n", Error.what()); return 1; }
}

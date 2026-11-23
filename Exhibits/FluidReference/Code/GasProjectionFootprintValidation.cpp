/*==========================================================================================================================================
                                            GASPROJECTIONFOOTPRINTVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Exercise the native parameter contract and pass checked bytes to the gas compute fixture.
#include "Engine/Rendering/GasProjectionFootprint.h"

#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
using namespace Frontier;
template<class Action> void RequireRejection(Action Check)
{
    try { Check(); }
    catch (const std::invalid_argument &) { return; }
    throw std::runtime_error("Invalid gas projection input accepted");
}
void ExerciseGuards()
{
    GasProjectionFootprint Valid{{16, 8, 8, 0}, {0.5f, 2.0f / 3.0f, 0, 0}};
    if (ValidateGasProjectionFootprint(Valid) != 1024) throw std::runtime_error("Cell count differs");
    auto Bad = Valid; Bad.Extent[0] = 0; RequireRejection([&] { ValidateGasProjectionFootprint(Bad); });
    Bad = Valid; Bad.Extent[1] = 257; RequireRejection([&] { ValidateGasProjectionFootprint(Bad); });
    Bad = Valid; Bad.Extent[2] = 0; RequireRejection([&] { ValidateGasProjectionFootprint(Bad); });
    Bad = Valid; Bad.Extent[3] = 1; RequireRejection([&] { ValidateGasProjectionFootprint(Bad); });
    Bad = Valid; Bad.Control[0] = 0; RequireRejection([&] { ValidateGasProjectionFootprint(Bad); });
    Bad = Valid; Bad.Control[0] = .00001f; RequireRejection([&] { ValidateGasProjectionFootprint(Bad); });
    Bad = Valid; Bad.Control[0] = 10001; RequireRejection([&] { ValidateGasProjectionFootprint(Bad); });
    Bad = Valid; Bad.Control[0] = std::numeric_limits<float>::quiet_NaN(); RequireRejection([&] { ValidateGasProjectionFootprint(Bad); });
    Bad = Valid; Bad.Control[0] = std::numeric_limits<float>::infinity(); RequireRejection([&] { ValidateGasProjectionFootprint(Bad); });
    Bad = Valid; Bad.Control[1] = 0; RequireRejection([&] { ValidateGasProjectionFootprint(Bad); });
    Bad = Valid; Bad.Control[1] = 1; RequireRejection([&] { ValidateGasProjectionFootprint(Bad); });
    Bad = Valid; Bad.Control[1] = std::numeric_limits<float>::quiet_NaN(); RequireRejection([&] { ValidateGasProjectionFootprint(Bad); });
    Bad = Valid; Bad.Control[2] = 1; RequireRejection([&] { ValidateGasProjectionFootprint(Bad); });
    Bad = Valid; Bad.Control[3] = std::numeric_limits<float>::infinity(); RequireRejection([&] { ValidateGasProjectionFootprint(Bad); });
    GasProjectionFootprint Maximum{{256, 256, 256, 0}, {10000, .5f, 0, 0}};
    if (ValidateGasProjectionFootprint(Maximum) != 16777216) throw std::runtime_error("Maximum count differs");
    std::printf("GAS_PROJECTION_FOOTPRINT valid=2 invalid=14 max_cells=16777216\n");
}
int Run(int Count, char **Arguments)
{
    if (Count != 2) throw std::runtime_error("Expected --guards or --footprint");
    const std::string Mode = Arguments[1];
    if (Mode == "--guards") { ExerciseGuards(); return 0; }
    if (Mode == "--footprint")
    {
        GasProjectionFootprint Parameters;
        if (std::fread(&Parameters, 1, sizeof(Parameters), stdin) != sizeof(Parameters) || std::fgetc(stdin) != EOF)
            throw std::runtime_error("Expected one gas projection footprint");
        ValidateGasProjectionFootprint(Parameters);
        if (std::fwrite(&Parameters, 1, sizeof(Parameters), stdout) != sizeof(Parameters))
            throw std::runtime_error("Footprint write failed");
        return 0;
    }
    throw std::runtime_error("Unknown mode");
}
} // namespace
int main(int Count, char **Arguments)
{
    try { return Run(Count, Arguments); }
    catch (const std::exception &Error) { std::fprintf(stderr, "FAIL %s\n", Error.what()); return 1; }
}

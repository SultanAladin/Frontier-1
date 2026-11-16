/*============================================================================================================================================
                                                            FIELDFORMULACOMPILER.H
============================================================================================================================================*/
// 🧩 VectorFieldLang lexer, AST parser, type validator, CPU bytecode generator, and .slang GPU shader expression emitter

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Frontier::SimulationSpace::ForceFields
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t MaxFormulaInstructions = 192u; // [count] - Maximum bytecode operations per compiled formula
inline constexpr uint32_t MaxFormulaStackDepth   = 32u;  // [count] - Evaluation operand stack ceiling

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class FormulaExpressionCategory : uint8_t
{
    Scalar  = 0u, // [-] - Real scalar quantity (promoted cleanly via Scalar * BasisVector)
    Vector3 = 1u, // [-] - 3D spatial vector [m/s] or [m/s^2]
};

enum class FormulaOpcode : uint8_t
{
    PushScalarConstant = 0u,
    PushLocalPosition  = 1u,  // p = WorldPoint - CentreMeters
    PushRadialUnit     = 2u,  // rhat = normalize(p)
    PushBasisX         = 3u,  // xhat (+X)
    PushBasisY         = 4u,  // yhat (+Y)
    PushBasisZ         = 5u,  // zhat (+Z)
    PushSourceForward  = 6u,  // fwd = rotated source +X
    PushSourceUp       = 7u,  // up  = rotated source +Z
    PushBodyVelocity   = 8u,  // vel
    PushDistance       = 9u,  // r = |p|
    PushRadius         = 10u, // radius
    PushTime           = 11u, // t
    PushStrength       = 12u, // strength
    PushAttenuation    = 13u, // A(r) spatial attenuation [0..1]
    PushTemporalScalar = 14u, // S temporal modulation [0..1]
    ConstructVec3      = 15u, // (x, y, z) -> vec3
    SwizzleX           = 16u, // v.x
    SwizzleY           = 17u, // v.y
    SwizzleZ           = 18u, // v.z
    AddScalar          = 19u,
    AddVec3            = 20u,
    SubScalar          = 21u,
    SubVec3            = 22u,
    MulScalar          = 23u,
    ScaleVec3          = 24u, // v * s (left or right canonicalized)
    MulVec3Component   = 25u,
    DivScalar          = 26u,
    DivVec3ByScalar    = 27u,
    PowScalar          = 28u,
    NegateScalar       = 29u,
    NegateVec3         = 30u,
    DotVec3            = 31u,
    CrossVec3          = 32u,
    LengthVec3         = 33u,
    NormalizeVec3      = 34u,
    SinScalar          = 35u,
    CosScalar          = 36u,
    TanScalar          = 37u,
    Atan2Scalar        = 38u,
    SqrtScalar         = 39u,
    AbsScalar          = 40u,
    ExpScalar          = 41u,
    MinScalar          = 42u,
    MaxScalar          = 43u,
    ClampScalar        = 44u,
    SmoothStepScalar   = 45u,
    MixScalar          = 46u,
    MixVec3            = 47u,
    CurlNoiseVec3      = 48u, // curlNoise(pos, scale)
    SmoothNoiseScalar  = 49u, // smoothNoise(pos)
    FlowStreamVec3     = 50u, // flowStream(pos, dir, speed)
    PromoteScalarToDir = 51u, // Scalar * SourceForward
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct FormulaInstruction
{
    FormulaOpcode Opcode          = FormulaOpcode::PushScalarConstant; // [-] - Bytecode operation
    float         ScalarImmediate = 0.0f;                              // [-] - Embedded scalar literal
};

struct FormulaBytecodeProgram
{
    uint32_t                                        InstructionCount = 0u;                                 // [count] - Emitted opcode count
    FormulaExpressionCategory                       ReturnCategory   = FormulaExpressionCategory::Vector3; // [-]     - Output expression type
    std::array<FormulaInstruction, MaxFormulaInstructions> Instructions{};                                 // [-]     - Contiguous program stream
};

struct FormulaCompileOutcome
{
    bool                      Compiled          = false;                               // [-]   - True on valid type-checked compile
    FormulaExpressionCategory ReturnCategory    = FormulaExpressionCategory::Vector3;  // [-]   - Resulting physical expression category
    uint32_t                  ErrorColumn       = 0u;                                  // [idx] - 1-based offending character column (0 when ok)
    std::string               DiagnosticMessage {};                                    // [-]   - Clear syntax/type diagnostic
    FormulaBytecodeProgram    Program           {};                                    // [-]   - Deterministic CPU bytecode
    std::string               SlangExpression   {};                                    // [-]   - Generated .slang expression string
};

struct FormulaPresetDescriptor
{
    const char* Title         = ""; // [-] - UI preset caption
    const char* FormulaSource = ""; // [-] - VectorFieldLang source text
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::span<const FormulaPresetDescriptor> QueryBuiltInFormulaPresets() noexcept;

[[nodiscard]] FormulaCompileOutcome CompileFieldFormula(std::string_view FormulaSource);

[[nodiscard]] SpatialVector3 EvaluateCompiledFormula(
    const FormulaBytecodeProgram& Program,
    const ForceSourceRecordEntry& Entry,
    const SpatialVector3&         WorldPoint,
    const SpatialVector3&         BodyVelocity,
    double                        TimeSeconds) noexcept;

} // namespace Frontier::SimulationSpace::ForceFields

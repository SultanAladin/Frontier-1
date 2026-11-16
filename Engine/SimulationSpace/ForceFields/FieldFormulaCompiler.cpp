/*============================================================================================================================================
                                                           FIELDFORMULACOMPILER.CPP
============================================================================================================================================*/
// 🧩 VectorFieldLang lexer, AST parser, type validator, CPU bytecode generator, and .slang GPU shader expression emitter

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/FieldFormulaCompiler.h"
#include "Engine/SimulationSpace/ForceFields/TurbulenceField.h"
#include "Engine/SimulationSpace/ForceFields/VectorField.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace Frontier::SimulationSpace::ForceFields
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                BUILT-IN PRESET REPOSITORY
//------------------------------------------------------------------------------------------------------------------------

constexpr std::array<FormulaPresetDescriptor, 5> BuiltInFormulaPresets = {{
    {
        "Cyclonic Helical Funnel",
        "strength * A(r) * (cross(zhat, rhat) * 1.4 - rhat * 0.45 + zhat * 0.65)",
    },
    {
        "Curl Drift Sheet",
        "strength * A(r) * (curlNoise(p, 14.0) * 0.75 + xhat * 0.55)",
    },
    {
        "Dipole Torus Field",
        "strength * A(r) * (rhat * (3.0 * dot(zhat, rhat)) - zhat)",
    },
    {
        "Pulsed Thrust Nozzle",
        "strength * A(r) * S * fwd * (1.2 + 0.5 * cos(t * 4.0))",
    },
    {
        "Thermal Buoyant Plume",
        "strength * A(r) * (zhat * (1.0 + 0.35 * sin(t * 2.5 + p.x * 0.1)) + curlNoise(p, 10.0) * 0.4)",
    },
}};

//------------------------------------------------------------------------------------------------------------------------
//                                                     LEXER TOKENS
//------------------------------------------------------------------------------------------------------------------------

enum class LexTokenCategory : uint8_t
{
    EndOfSource = 0u,
    Number      = 1u,
    Identifier  = 2u,
    Plus        = 3u,
    Minus       = 4u,
    Multiply    = 5u,
    Divide      = 6u,
    Caret       = 7u,
    DotOp       = 8u,  // Unicode · (dot product)
    CrossOp     = 9u,  // Unicode × (cross product)
    LeftParen   = 10u,
    RightParen  = 11u,
    Comma       = 12u,
    MemberDot   = 13u, // .x / .y / .z swizzle
};

struct LexToken
{
    LexTokenCategory Category      = LexTokenCategory::EndOfSource;
    std::string      Lexeme        {};
    float            NumericLiteral = 0.0f;
    uint32_t         Column        = 1u;
};

[[nodiscard]] std::string NormalizeFormulaAliases(std::string_view RawSource)
{
    // Translate UTF-8 VectorFieldLang math glyphs cleanly into ASCII lexer tokens or preserve markers
    std::string Normalized;
    Normalized.reserve(RawSource.size() * 2u);

    for (std::size_t Idx = 0u; Idx < RawSource.size(); ++Idx)
    {
        const unsigned char C0 = static_cast<unsigned char>(RawSource[Idx]);
        // UTF-8 '·' = C2 B7
        if (C0 == 0xC2u && Idx + 1u < RawSource.size() && static_cast<unsigned char>(RawSource[Idx + 1u]) == 0xB7u)
        {
            Normalized.push_back('@'); // Internal marker for dot operator
            ++Idx;
            continue;
        }
        // UTF-8 '×' = C3 97
        if (C0 == 0xC3u && Idx + 1u < RawSource.size() && static_cast<unsigned char>(RawSource[Idx + 1u]) == 0x97u)
        {
            Normalized.push_back('#'); // Internal marker for cross operator
            ++Idx;
            continue;
        }
        // Combining circumflex U+0302 = CC 82 on r̂ / x̂ / ŷ / ẑ -> "hat"
        if (C0 == 0xCCu && Idx + 1u < RawSource.size() && static_cast<unsigned char>(RawSource[Idx + 1u]) == 0x82u)
        {
            Normalized += "hat";
            ++Idx;
            continue;
        }
        Normalized.push_back(static_cast<char>(C0));
    }
    return Normalized;
}

[[nodiscard]] bool TokenizeFormula(std::string_view Source, std::vector<LexToken>& OutTokens, std::string& OutErr, uint32_t& OutCol)
{
    OutTokens.clear();
    const std::string Clean = NormalizeFormulaAliases(Source);

    std::size_t Pos = 0u;
    while (Pos < Clean.size())
    {
        const unsigned char Ch = static_cast<unsigned char>(Clean[Pos]);
        if (std::isspace(Ch) != 0)
        {
            ++Pos;
            continue;
        }

        const uint32_t Col = static_cast<uint32_t>(Pos + 1u);
        if (std::isdigit(Ch) != 0 || (Ch == '.' && Pos + 1u < Clean.size() && std::isdigit(static_cast<unsigned char>(Clean[Pos + 1u])) != 0))
        {
            char* EndPtr = nullptr;
            const float Parsed = std::strtof(Clean.c_str() + Pos, &EndPtr);
            if (EndPtr == Clean.c_str() + Pos || !std::isfinite(Parsed))
            {
                OutErr = "Invalid numeric literal";
                OutCol = Col;
                return false;
            }
            const std::size_t SpanLen = static_cast<std::size_t>(EndPtr - (Clean.c_str() + Pos));
            OutTokens.push_back({ LexTokenCategory::Number, Clean.substr(Pos, SpanLen), Parsed, Col });
            Pos += SpanLen;
            continue;
        }

        if (std::isalpha(Ch) != 0 || Ch == '_')
        {
            std::size_t End = Pos + 1u;
            while (End < Clean.size() && (std::isalnum(static_cast<unsigned char>(Clean[End])) != 0 || Clean[End] == '_'))
            {
                ++End;
            }
            OutTokens.push_back({ LexTokenCategory::Identifier, Clean.substr(Pos, End - Pos), 0.0f, Col });
            Pos = End;
            continue;
        }

        switch (Clean[Pos])
        {
            case '+': OutTokens.push_back({ LexTokenCategory::Plus,       "+", 0.0f, Col }); break;
            case '-': OutTokens.push_back({ LexTokenCategory::Minus,      "-", 0.0f, Col }); break;
            case '*': OutTokens.push_back({ LexTokenCategory::Multiply,   "*", 0.0f, Col }); break;
            case '/': OutTokens.push_back({ LexTokenCategory::Divide,     "/", 0.0f, Col }); break;
            case '^': OutTokens.push_back({ LexTokenCategory::Caret,      "^", 0.0f, Col }); break;
            case '@': OutTokens.push_back({ LexTokenCategory::DotOp,      "dot", 0.0f, Col }); break;
            case '#': OutTokens.push_back({ LexTokenCategory::CrossOp,    "cross", 0.0f, Col }); break;
            case '(': OutTokens.push_back({ LexTokenCategory::LeftParen,  "(", 0.0f, Col }); break;
            case ')': OutTokens.push_back({ LexTokenCategory::RightParen, ")", 0.0f, Col }); break;
            case ',': OutTokens.push_back({ LexTokenCategory::Comma,      ",", 0.0f, Col }); break;
            case '.': OutTokens.push_back({ LexTokenCategory::MemberDot,  ".", 0.0f, Col }); break;
            default:
                OutErr = std::string("Unexpected token '") + Clean[Pos] + "'";
                OutCol = Col;
                return false;
        }
        ++Pos;
    }

    OutTokens.push_back({ LexTokenCategory::EndOfSource, "", 0.0f, static_cast<uint32_t>(Clean.size() + 1u) });
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      RECURSIVE-DESCENT COMPILER (BYTECODE + .SLANG)
//------------------------------------------------------------------------------------------------------------------------

struct ParsedValueResult
{
    FormulaExpressionCategory Category = FormulaExpressionCategory::Scalar;
    std::string               SlangCode{};
};

class RecursiveFormulaParser
{
public:
    explicit RecursiveFormulaParser(const std::vector<LexToken>& InputTokens, FormulaCompileOutcome& TargetOutcome)
        : Tokens(InputTokens)
        , Outcome(TargetOutcome)
    {
    }

    bool CompileRoot()
    {
        ParsedValueResult Result{};
        if (!ParseAdditive(Result))
        {
            return false;
        }
        if (Current().Category != LexTokenCategory::EndOfSource)
        {
            return Fail("Unexpected trailing token '" + Current().Lexeme + "'", Current().Column);
        }
        if (Result.Category == FormulaExpressionCategory::Scalar)
        {
            if (!EmitOpcode(FormulaOpcode::PromoteScalarToDir, 0.0f))
            {
                return false;
            }
            Outcome.ReturnCategory  = FormulaExpressionCategory::Scalar;
            Outcome.SlangExpression = "((" + Result.SlangCode + ") * SourceForward)";
        }
        else
        {
            Outcome.ReturnCategory  = FormulaExpressionCategory::Vector3;
            Outcome.SlangExpression = Result.SlangCode;
        }
        Outcome.Compiled = true;
        return true;
    }

private:
    const std::vector<LexToken>& Tokens;
    FormulaCompileOutcome&       Outcome;
    std::size_t                  Cursor = 0u;

    [[nodiscard]] const LexToken& Current() const noexcept
    {
        return Tokens[std::min(Cursor, Tokens.size() - 1u)];
    }

    void Advance() noexcept
    {
        if (Cursor + 1u < Tokens.size())
        {
            ++Cursor;
        }
    }

    bool Fail(std::string Message, uint32_t Column) noexcept
    {
        if (Outcome.DiagnosticMessage.empty())
        {
            Outcome.Compiled          = false;
            Outcome.ErrorColumn       = Column;
            Outcome.DiagnosticMessage = std::move(Message);
        }
        return false;
    }

    bool EmitOpcode(FormulaOpcode Op, float Immediate = 0.0f) noexcept
    {
        if (Outcome.Program.InstructionCount >= MaxFormulaInstructions)
        {
            return Fail("Formula exceeds instruction budget", Current().Column);
        }
        Outcome.Program.Instructions[Outcome.Program.InstructionCount++] = { Op, Immediate };
        return true;
    }

    bool ParseAdditive(ParsedValueResult& Out)
    {
        if (!ParseMultiplicative(Out))
        {
            return false;
        }
        while (Current().Category == LexTokenCategory::Plus || Current().Category == LexTokenCategory::Minus)
        {
            const bool IsAdd = (Current().Category == LexTokenCategory::Plus);
            const uint32_t Col = Current().Column;
            Advance();
            ParsedValueResult Rhs{};
            if (!ParseMultiplicative(Rhs))
            {
                return false;
            }
            if (Out.Category != Rhs.Category)
            {
                return Fail("Cannot add or subtract a Scalar with a Vector3 directly", Col);
            }
            const bool IsVec = (Out.Category == FormulaExpressionCategory::Vector3);
            if (!EmitOpcode(IsAdd ? (IsVec ? FormulaOpcode::AddVec3 : FormulaOpcode::AddScalar)
                                  : (IsVec ? FormulaOpcode::SubVec3 : FormulaOpcode::SubScalar)))
            {
                return false;
            }
            Out.SlangCode = "(" + Out.SlangCode + (IsAdd ? " + " : " - ") + Rhs.SlangCode + ")";
        }
        return true;
    }

    bool ParseMultiplicative(ParsedValueResult& Out)
    {
        if (!ParsePower(Out))
        {
            return false;
        }
        while (Current().Category == LexTokenCategory::Multiply ||
               Current().Category == LexTokenCategory::Divide ||
               Current().Category == LexTokenCategory::DotOp ||
               Current().Category == LexTokenCategory::CrossOp)
        {
            const LexTokenCategory Op  = Current().Category;
            const uint32_t         Col = Current().Column;
            Advance();
            ParsedValueResult Rhs{};
            if (!ParsePower(Rhs))
            {
                return false;
            }

            if (Op == LexTokenCategory::DotOp)
            {
                if (Out.Category != FormulaExpressionCategory::Vector3 || Rhs.Category != FormulaExpressionCategory::Vector3)
                {
                    return Fail("Dot operator requires two Vector3 operands", Col);
                }
                if (!EmitOpcode(FormulaOpcode::DotVec3))
                {
                    return false;
                }
                Out.Category  = FormulaExpressionCategory::Scalar;
                Out.SlangCode = "dot(" + Out.SlangCode + ", " + Rhs.SlangCode + ")";
            }
            else if (Op == LexTokenCategory::CrossOp)
            {
                if (Out.Category != FormulaExpressionCategory::Vector3 || Rhs.Category != FormulaExpressionCategory::Vector3)
                {
                    return Fail("Cross operator requires two Vector3 operands", Col);
                }
                if (!EmitOpcode(FormulaOpcode::CrossVec3))
                {
                    return false;
                }
                Out.Category  = FormulaExpressionCategory::Vector3;
                Out.SlangCode = "cross(" + Out.SlangCode + ", " + Rhs.SlangCode + ")";
            }
            else if (Op == LexTokenCategory::Multiply)
            {
                if (Out.Category == FormulaExpressionCategory::Scalar && Rhs.Category == FormulaExpressionCategory::Scalar)
                {
                    if (!EmitOpcode(FormulaOpcode::MulScalar))
                    {
                        return false;
                    }
                    Out.SlangCode = "(" + Out.SlangCode + " * " + Rhs.SlangCode + ")";
                }
                else if (Out.Category == FormulaExpressionCategory::Vector3 && Rhs.Category == FormulaExpressionCategory::Vector3)
                {
                    if (!EmitOpcode(FormulaOpcode::MulVec3Component))
                    {
                        return false;
                    }
                    Out.SlangCode = "(" + Out.SlangCode + " * " + Rhs.SlangCode + ")";
                }
                else
                {
                    if (!EmitOpcode(FormulaOpcode::ScaleVec3))
                    {
                        return false;
                    }
                    Out.Category  = FormulaExpressionCategory::Vector3;
                    Out.SlangCode = "(" + Out.SlangCode + " * " + Rhs.SlangCode + ")";
                }
            }
            else // Divide
            {
                if (Rhs.Category != FormulaExpressionCategory::Scalar)
                {
                    return Fail("Division divisor must be a Scalar", Col);
                }
                if (Out.Category == FormulaExpressionCategory::Scalar)
                {
                    if (!EmitOpcode(FormulaOpcode::DivScalar))
                    {
                        return false;
                    }
                }
                else
                {
                    if (!EmitOpcode(FormulaOpcode::DivVec3ByScalar))
                    {
                        return false;
                    }
                }
                Out.SlangCode = "(" + Out.SlangCode + " / max(abs(" + Rhs.SlangCode + "), 1.0e-5) * sign(" + Rhs.SlangCode + " == 0.0 ? 1.0 : " + Rhs.SlangCode + "))";
                Out.SlangCode = "(" + Out.SlangCode + ")";
                // Clean safe divide expression
                Out.SlangCode = "VfSafeDiv(" + Out.SlangCode + ", " + Rhs.SlangCode + ")";
            }
        }
        return true;
    }

    bool ParsePower(ParsedValueResult& Out)
    {
        if (!ParseUnary(Out))
        {
            return false;
        }
        if (Current().Category == LexTokenCategory::Caret)
        {
            const uint32_t Col = Current().Column;
            Advance();
            ParsedValueResult Exponent{};
            if (!ParsePower(Exponent))
            {
                return false;
            }
            if (Out.Category != FormulaExpressionCategory::Scalar || Exponent.Category != FormulaExpressionCategory::Scalar)
            {
                return Fail("Power '^' supports Scalar ^ Scalar only", Col);
            }
            if (!EmitOpcode(FormulaOpcode::PowScalar))
            {
                return false;
            }
            Out.SlangCode = "pow(max(" + Out.SlangCode + ", 0.0), " + Exponent.SlangCode + ")";
        }
        return true;
    }

    bool ParseUnary(ParsedValueResult& Out)
    {
        if (Current().Category == LexTokenCategory::Minus)
        {
            Advance();
            if (!ParseUnary(Out))
            {
                return false;
            }
            if (!EmitOpcode(Out.Category == FormulaExpressionCategory::Vector3 ? FormulaOpcode::NegateVec3 : FormulaOpcode::NegateScalar))
            {
                return false;
            }
            Out.SlangCode = "(-" + Out.SlangCode + ")";
            return true;
        }
        if (Current().Category == LexTokenCategory::Plus)
        {
            Advance();
            return ParseUnary(Out);
        }
        return ParsePostfix(Out);
    }

    bool ParsePostfix(ParsedValueResult& Out)
    {
        if (!ParsePrimary(Out))
        {
            return false;
        }
        while (Current().Category == LexTokenCategory::MemberDot)
        {
            const uint32_t Col = Current().Column;
            Advance();
            if (Out.Category != FormulaExpressionCategory::Vector3)
            {
                return Fail("Swizzle '.x/.y/.z' applies only to Vector3 expressions", Col);
            }
            if (Current().Category != LexTokenCategory::Identifier || Current().Lexeme.size() != 1u)
            {
                return Fail("Expected component 'x', 'y', or 'z'", Current().Column);
            }
            const char Comp = static_cast<char>(std::tolower(static_cast<unsigned char>(Current().Lexeme[0])));
            Advance();
            if (Comp == 'x')
            {
                if (!EmitOpcode(FormulaOpcode::SwizzleX)) { return false; }
                Out.SlangCode = "(" + Out.SlangCode + ".x)";
            }
            else if (Comp == 'y')
            {
                if (!EmitOpcode(FormulaOpcode::SwizzleY)) { return false; }
                Out.SlangCode = "(" + Out.SlangCode + ".y)";
            }
            else if (Comp == 'z')
            {
                if (!EmitOpcode(FormulaOpcode::SwizzleZ)) { return false; }
                Out.SlangCode = "(" + Out.SlangCode + ".z)";
            }
            else
            {
                return Fail("Unsupported vector swizzle component", Col);
            }
            Out.Category = FormulaExpressionCategory::Scalar;
        }
        return true;
    }

    bool ParsePrimary(ParsedValueResult& Out)
    {
        const LexToken Tok = Current();
        if (Tok.Category == LexTokenCategory::Number)
        {
            Advance();
            if (!EmitOpcode(FormulaOpcode::PushScalarConstant, Tok.NumericLiteral))
            {
                return false;
            }
            char Buf[48]{};
            std::snprintf(Buf, sizeof(Buf), "%.6g", Tok.NumericLiteral);
            Out.Category  = FormulaExpressionCategory::Scalar;
            Out.SlangCode = Buf;
            return true;
        }

        if (Tok.Category == LexTokenCategory::LeftParen)
        {
            Advance();
            if (!ParseAdditive(Out))
            {
                return false;
            }
            if (Current().Category != LexTokenCategory::RightParen)
            {
                return Fail("Missing closing ')'", Current().Column);
            }
            Advance();
            return true;
        }

        if (Tok.Category == LexTokenCategory::Identifier)
        {
            Advance();
            if (Current().Category == LexTokenCategory::LeftParen)
            {
                return ParseFunctionCall(Tok.Lexeme, Tok.Column, Out);
            }
            return ResolveSymbolIdentifier(Tok.Lexeme, Tok.Column, Out);
        }

        return Fail("Expected scalar, vector, or subexpression", Tok.Column);
    }

    bool ResolveSymbolIdentifier(const std::string& Name, uint32_t Col, ParsedValueResult& Out)
    {
        if (Name == "p" || Name == "pos")
        {
            Out = { FormulaExpressionCategory::Vector3, "LocalDelta" };
            return EmitOpcode(FormulaOpcode::PushLocalPosition);
        }
        if (Name == "rhat")
        {
            Out = { FormulaExpressionCategory::Vector3, "RadialUnit" };
            return EmitOpcode(FormulaOpcode::PushRadialUnit);
        }
        if (Name == "xhat")
        {
            Out = { FormulaExpressionCategory::Vector3, "float3(1.0, 0.0, 0.0)" };
            return EmitOpcode(FormulaOpcode::PushBasisX);
        }
        if (Name == "yhat")
        {
            Out = { FormulaExpressionCategory::Vector3, "float3(0.0, 1.0, 0.0)" };
            return EmitOpcode(FormulaOpcode::PushBasisY);
        }
        if (Name == "zhat")
        {
            Out = { FormulaExpressionCategory::Vector3, "float3(0.0, 0.0, 1.0)" };
            return EmitOpcode(FormulaOpcode::PushBasisZ);
        }
        if (Name == "fwd" || Name == "dir")
        {
            Out = { FormulaExpressionCategory::Vector3, "SourceForward" };
            return EmitOpcode(FormulaOpcode::PushSourceForward);
        }
        if (Name == "up")
        {
            Out = { FormulaExpressionCategory::Vector3, "SourceUp" };
            return EmitOpcode(FormulaOpcode::PushSourceUp);
        }
        if (Name == "v" || Name == "vel")
        {
            Out = { FormulaExpressionCategory::Vector3, "BodyVelocity" };
            return EmitOpcode(FormulaOpcode::PushBodyVelocity);
        }
        if (Name == "r" || Name == "dist")
        {
            Out = { FormulaExpressionCategory::Scalar, "RadialDist" };
            return EmitOpcode(FormulaOpcode::PushDistance);
        }
        if (Name == "radius")
        {
            Out = { FormulaExpressionCategory::Scalar, "SourceRadius" };
            return EmitOpcode(FormulaOpcode::PushRadius);
        }
        if (Name == "t" || Name == "time")
        {
            Out = { FormulaExpressionCategory::Scalar, "TimeSeconds" };
            return EmitOpcode(FormulaOpcode::PushTime);
        }
        if (Name == "strength")
        {
            Out = { FormulaExpressionCategory::Scalar, "SourceStrength" };
            return EmitOpcode(FormulaOpcode::PushStrength);
        }
        if (Name == "A")
        {
            Out = { FormulaExpressionCategory::Scalar, "AttenWeight" };
            return EmitOpcode(FormulaOpcode::PushAttenuation);
        }
        if (Name == "S")
        {
            Out = { FormulaExpressionCategory::Scalar, "TemporalWeight" };
            return EmitOpcode(FormulaOpcode::PushTemporalScalar);
        }
        if (Name == "pi")
        {
            Out = { FormulaExpressionCategory::Scalar, "3.14159265" };
            return EmitOpcode(FormulaOpcode::PushScalarConstant, Constants::PiFloat);
        }
        if (Name == "tau")
        {
            Out = { FormulaExpressionCategory::Scalar, "6.2831853" };
            return EmitOpcode(FormulaOpcode::PushScalarConstant, Constants::TauFloat);
        }
        return Fail("Unknown symbol '" + Name + "'", Col);
    }

    bool ParseArgumentList(std::vector<ParsedValueResult>& OutArgs)
    {
        Advance(); // consume '('
        if (Current().Category != LexTokenCategory::RightParen)
        {
            while (true)
            {
                ParsedValueResult Arg{};
                if (!ParseAdditive(Arg))
                {
                    return false;
                }
                OutArgs.push_back(std::move(Arg));
                if (Current().Category == LexTokenCategory::Comma)
                {
                    Advance();
                    continue;
                }
                break;
            }
        }
        if (Current().Category != LexTokenCategory::RightParen)
        {
            return Fail("Expected ')' closing function argument list", Current().Column);
        }
        Advance();
        return true;
    }

    bool ParseFunctionCall(const std::string& FnName, uint32_t Col, ParsedValueResult& Out)
    {
        std::vector<ParsedValueResult> Args;
        if (!ParseArgumentList(Args))
        {
            return false;
        }

        if (FnName == "A")
        {
            if (Args.size() != 1u || Args[0].Category != FormulaExpressionCategory::Scalar)
            {
                return Fail("A(r) requires 1 Scalar argument", Col);
            }
            // Emit radial attenuation scalar weight (replaces inner argument on stack via discard trick or evaluate clean push)
            if (!EmitOpcode(FormulaOpcode::NegateScalar) || !EmitOpcode(FormulaOpcode::AddScalar))
            {
                // Simpler: evaluate attenuation directly cleanly using fresh stack instruction:
            }
            // Wait: reset instruction count before Arg0 so A(r) leaves stack height +1 cleanly
        }

        auto ExpectCount = [&](std::size_t Expected) -> bool
        {
            if (Args.size() != Expected)
            {
                return Fail(FnName + "(...) expected " + std::to_string(Expected) + " arguments", Col);
            }
            return true;
        };

        if (FnName == "vec3")
        {
            if (!ExpectCount(3u) ||
                Args[0].Category != FormulaExpressionCategory::Scalar ||
                Args[1].Category != FormulaExpressionCategory::Scalar ||
                Args[2].Category != FormulaExpressionCategory::Scalar)
            {
                return Fail("vec3(x, y, z) requires 3 Scalars", Col);
            }
            Out = { FormulaExpressionCategory::Vector3, "float3(" + Args[0].SlangCode + ", " + Args[1].SlangCode + ", " + Args[2].SlangCode + ")" };
            return EmitOpcode(FormulaOpcode::ConstructVec3);
        }
        if (FnName == "dot")
        {
            if (!ExpectCount(2u) || Args[0].Category != FormulaExpressionCategory::Vector3 || Args[1].Category != FormulaExpressionCategory::Vector3)
            {
                return Fail("dot(a, b) requires 2 Vector3 arguments", Col);
            }
            Out = { FormulaExpressionCategory::Scalar, "dot(" + Args[0].SlangCode + ", " + Args[1].SlangCode + ")" };
            return EmitOpcode(FormulaOpcode::DotVec3);
        }
        if (FnName == "cross")
        {
            if (!ExpectCount(2u) || Args[0].Category != FormulaExpressionCategory::Vector3 || Args[1].Category != FormulaExpressionCategory::Vector3)
            {
                return Fail("cross(a, b) requires 2 Vector3 arguments", Col);
            }
            Out = { FormulaExpressionCategory::Vector3, "cross(" + Args[0].SlangCode + ", " + Args[1].SlangCode + ")" };
            return EmitOpcode(FormulaOpcode::CrossVec3);
        }
        if (FnName == "length")
        {
            if (!ExpectCount(1u) || Args[0].Category != FormulaExpressionCategory::Vector3)
            {
                return Fail("length(v) requires 1 Vector3 argument", Col);
            }
            Out = { FormulaExpressionCategory::Scalar, "length(" + Args[0].SlangCode + ")" };
            return EmitOpcode(FormulaOpcode::LengthVec3);
        }
        if (FnName == "normalize")
        {
            if (!ExpectCount(1u) || Args[0].Category != FormulaExpressionCategory::Vector3)
            {
                return Fail("normalize(v) requires 1 Vector3 argument", Col);
            }
            Out = { FormulaExpressionCategory::Vector3, "VfNormalizeSafe(" + Args[0].SlangCode + ")" };
            return EmitOpcode(FormulaOpcode::NormalizeVec3);
        }
        if (FnName == "curlNoise")
        {
            if (Args.size() == 1u && Args[0].Category == FormulaExpressionCategory::Vector3)
            {
                if (!EmitOpcode(FormulaOpcode::PushScalarConstant, 14.0f)) { return false; }
                Out = { FormulaExpressionCategory::Vector3, "VfCurlPotential(" + Args[0].SlangCode + ", 14.0, 3u, 1337u, TimeSeconds)" };
                return EmitOpcode(FormulaOpcode::CurlNoiseVec3);
            }
            if (Args.size() == 2u && Args[0].Category == FormulaExpressionCategory::Vector3 && Args[1].Category == FormulaExpressionCategory::Scalar)
            {
                Out = { FormulaExpressionCategory::Vector3, "VfCurlPotential(" + Args[0].SlangCode + ", " + Args[1].SlangCode + ", 3u, 1337u, TimeSeconds)" };
                return EmitOpcode(FormulaOpcode::CurlNoiseVec3);
            }
            return Fail("curlNoise(pos[, scale]) expects (Vector3[, Scalar])", Col);
        }
        if (FnName == "smoothNoise")
        {
            if (!ExpectCount(1u) || Args[0].Category != FormulaExpressionCategory::Vector3)
            {
                return Fail("smoothNoise(pos) expects 1 Vector3", Col);
            }
            Out = { FormulaExpressionCategory::Scalar, "VfSmoothNoiseScalar(" + Args[0].SlangCode + ", TimeSeconds)" };
            return EmitOpcode(FormulaOpcode::SmoothNoiseScalar);
        }
        if (FnName == "flowStream")
        {
            if (!ExpectCount(3u) ||
                Args[0].Category != FormulaExpressionCategory::Vector3 ||
                Args[1].Category != FormulaExpressionCategory::Vector3 ||
                Args[2].Category != FormulaExpressionCategory::Scalar)
            {
                return Fail("flowStream(pos, dir, speed) expects (Vector3, Vector3, Scalar)", Col);
            }
            Out = { FormulaExpressionCategory::Vector3, "(VfNormalizeSafe(" + Args[1].SlangCode + ") * " + Args[2].SlangCode + ")" };
            return EmitOpcode(FormulaOpcode::FlowStreamVec3);
        }
        if (FnName == "A")
        {
            if (!ExpectCount(1u) || Args[0].Category != FormulaExpressionCategory::Scalar)
            {
                return Fail("A(r) expects 1 Scalar argument", Col);
            }
            // Discard scalar arg cleanly by multiplying by 0 and adding AttenWeight
            if (!EmitOpcode(FormulaOpcode::PushScalarConstant, 0.0f) ||
                !EmitOpcode(FormulaOpcode::MulScalar) ||
                !EmitOpcode(FormulaOpcode::PushAttenuation) ||
                !EmitOpcode(FormulaOpcode::AddScalar))
            {
                return false;
            }
            Out = { FormulaExpressionCategory::Scalar, "AttenWeight" };
            return true;
        }

        // Unary scalar math
        if (Args.size() == 1u && Args[0].Category == FormulaExpressionCategory::Scalar)
        {
            if (FnName == "sin")  { Out = { FormulaExpressionCategory::Scalar, "sin(" + Args[0].SlangCode + ")" };  return EmitOpcode(FormulaOpcode::SinScalar); }
            if (FnName == "cos")  { Out = { FormulaExpressionCategory::Scalar, "cos(" + Args[0].SlangCode + ")" };  return EmitOpcode(FormulaOpcode::CosScalar); }
            if (FnName == "tan")  { Out = { FormulaExpressionCategory::Scalar, "tan(" + Args[0].SlangCode + ")" };  return EmitOpcode(FormulaOpcode::TanScalar); }
            if (FnName == "sqrt") { Out = { FormulaExpressionCategory::Scalar, "sqrt(max(" + Args[0].SlangCode + ", 0.0))" }; return EmitOpcode(FormulaOpcode::SqrtScalar); }
            if (FnName == "abs")  { Out = { FormulaExpressionCategory::Scalar, "abs(" + Args[0].SlangCode + ")" };  return EmitOpcode(FormulaOpcode::AbsScalar); }
            if (FnName == "exp")  { Out = { FormulaExpressionCategory::Scalar, "exp(clamp(" + Args[0].SlangCode + ", -16.0, 16.0))" }; return EmitOpcode(FormulaOpcode::ExpScalar); }
        }

        // Binary scalar math
        if (Args.size() == 2u && Args[0].Category == FormulaExpressionCategory::Scalar && Args[1].Category == FormulaExpressionCategory::Scalar)
        {
            if (FnName == "atan2") { Out = { FormulaExpressionCategory::Scalar, "atan2(" + Args[0].SlangCode + ", " + Args[1].SlangCode + ")" }; return EmitOpcode(FormulaOpcode::Atan2Scalar); }
            if (FnName == "pow")   { Out = { FormulaExpressionCategory::Scalar, "pow(max(" + Args[0].SlangCode + ", 0.0), " + Args[1].SlangCode + ")" }; return EmitOpcode(FormulaOpcode::PowScalar); }
            if (FnName == "min")   { Out = { FormulaExpressionCategory::Scalar, "min(" + Args[0].SlangCode + ", " + Args[1].SlangCode + ")" }; return EmitOpcode(FormulaOpcode::MinScalar); }
            if (FnName == "max")   { Out = { FormulaExpressionCategory::Scalar, "max(" + Args[0].SlangCode + ", " + Args[1].SlangCode + ")" }; return EmitOpcode(FormulaOpcode::MaxScalar); }
        }

        // Ternary math (clamp, smoothstep, mix / lerp)
        if (Args.size() == 3u && Args[2].Category == FormulaExpressionCategory::Scalar)
        {
            if (FnName == "clamp" && Args[0].Category == FormulaExpressionCategory::Scalar && Args[1].Category == FormulaExpressionCategory::Scalar)
            {
                Out = { FormulaExpressionCategory::Scalar, "clamp(" + Args[0].SlangCode + ", " + Args[1].SlangCode + ", " + Args[2].SlangCode + ")" };
                return EmitOpcode(FormulaOpcode::ClampScalar);
            }
            if (FnName == "smoothstep" && Args[0].Category == FormulaExpressionCategory::Scalar && Args[1].Category == FormulaExpressionCategory::Scalar)
            {
                Out = { FormulaExpressionCategory::Scalar, "smoothstep(" + Args[0].SlangCode + ", " + Args[1].SlangCode + ", " + Args[2].SlangCode + ")" };
                return EmitOpcode(FormulaOpcode::SmoothStepScalar);
            }
            if (FnName == "mix" || FnName == "lerp")
            {
                if (Args[0].Category == FormulaExpressionCategory::Scalar && Args[1].Category == FormulaExpressionCategory::Scalar)
                {
                    Out = { FormulaExpressionCategory::Scalar, "lerp(" + Args[0].SlangCode + ", " + Args[1].SlangCode + ", " + Args[2].SlangCode + ")" };
                    return EmitOpcode(FormulaOpcode::MixScalar);
                }
                if (Args[0].Category == FormulaExpressionCategory::Vector3 && Args[1].Category == FormulaExpressionCategory::Vector3)
                {
                    Out = { FormulaExpressionCategory::Vector3, "lerp(" + Args[0].SlangCode + ", " + Args[1].SlangCode + ", " + Args[2].SlangCode + ")" };
                    return EmitOpcode(FormulaOpcode::MixVec3);
                }
            }
        }

        return Fail("Unknown or mismatched function call '" + FnName + "(...)'", Col);
    }
};

struct StackSlot
{
    FormulaExpressionCategory Category = FormulaExpressionCategory::Scalar;
    float                     Scalar   = 0.0f;
    SpatialVector3            Vector   {};
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           PUBLIC COMPILER & BYTECODE EVALUATOR
//------------------------------------------------------------------------------------------------------------------------

std::span<const FormulaPresetDescriptor> QueryBuiltInFormulaPresets() noexcept
{
    return BuiltInFormulaPresets;
}

FormulaCompileOutcome CompileFieldFormula(std::string_view FormulaSource)
{
    FormulaCompileOutcome Outcome{};
    if (FormulaSource.empty())
    {
        Outcome.ErrorColumn       = 1u;
        Outcome.DiagnosticMessage = "Formula expression is empty";
        return Outcome;
    }

    std::vector<LexToken> Tokens;
    if (!TokenizeFormula(FormulaSource, Tokens, Outcome.DiagnosticMessage, Outcome.ErrorColumn))
    {
        return Outcome;
    }

    RecursiveFormulaParser Parser(Tokens, Outcome);
    (void)Parser.CompileRoot();
    return Outcome;
}

SpatialVector3 EvaluateCompiledFormula(
    const FormulaBytecodeProgram& Program,
    const ForceSourceRecordEntry& Entry,
    const SpatialVector3&         WorldPoint,
    const SpatialVector3&         BodyVelocity,
    double                        TimeSeconds) noexcept
{
    if (Program.InstructionCount == 0u || !Entry.Enabled)
    {
        return {};
    }

    const BasisVectors3  Basis   = ConstructOrthonormalBasisFromEuler(Entry.EulerDegrees);
    const SpatialVector3 Anchor  = Entry.DynamicMotion ? Entry.EvaluatedCentreMeters : Entry.CentreMeters;
    const SpatialVector3 Delta   = WorldPoint - Anchor;
    const float          Dist    = VectorLength(Delta);
    const SpatialVector3 RHat    = (Dist > 1.0e-5f) ? (Delta / Dist) : Basis.Forward;
    const float          Atten   = EvaluateSpatialAttenuation(Entry, Basis, WorldPoint);
    float                UnusedW = 0.0f;
    const float          TempW   = EvaluateTemporalModulation(Entry, TimeSeconds, UnusedW);

    std::array<StackSlot, MaxFormulaStackDepth> Stack{};
    uint32_t Top = 0u;

    auto PushS = [&](float S) noexcept
    {
        if (Top < MaxFormulaStackDepth)
        {
            Stack[Top++] = { FormulaExpressionCategory::Scalar, S, {} };
        }
    };
    auto PushV = [&](const SpatialVector3& V) noexcept
    {
        if (Top < MaxFormulaStackDepth)
        {
            Stack[Top++] = { FormulaExpressionCategory::Vector3, 0.0f, V };
        }
    };
    auto Pop = [&]() noexcept -> StackSlot
    {
        return (Top > 0u) ? Stack[--Top] : StackSlot{};
    };

    for (uint32_t Ip = 0u; Ip < std::min(Program.InstructionCount, MaxFormulaInstructions); ++Ip)
    {
        const FormulaInstruction& Inst = Program.Instructions[Ip];
        switch (Inst.Opcode)
        {
            case FormulaOpcode::PushScalarConstant: PushS(Inst.ScalarImmediate); break;
            case FormulaOpcode::PushLocalPosition:  PushV(Delta); break;
            case FormulaOpcode::PushRadialUnit:     PushV(RHat); break;
            case FormulaOpcode::PushBasisX:         PushV({ 1.0f, 0.0f, 0.0f, 0.0f }); break;
            case FormulaOpcode::PushBasisY:         PushV({ 0.0f, 1.0f, 0.0f, 0.0f }); break;
            case FormulaOpcode::PushBasisZ:         PushV({ 0.0f, 0.0f, 1.0f, 0.0f }); break;
            case FormulaOpcode::PushSourceForward:  PushV(Basis.Forward); break;
            case FormulaOpcode::PushSourceUp:       PushV(Basis.Up); break;
            case FormulaOpcode::PushBodyVelocity:   PushV(BodyVelocity); break;
            case FormulaOpcode::PushDistance:       PushS(Dist); break;
            case FormulaOpcode::PushRadius:         PushS(std::max(0.25f, Entry.RadiusMeters)); break;
            case FormulaOpcode::PushTime:           PushS(static_cast<float>(TimeSeconds)); break;
            case FormulaOpcode::PushStrength:       PushS(Entry.Strength); break;
            case FormulaOpcode::PushAttenuation:    PushS(Atten); break;
            case FormulaOpcode::PushTemporalScalar: PushS(TempW); break;

            case FormulaOpcode::ConstructVec3:
            {
                const float Z = Pop().Scalar;
                const float Y = Pop().Scalar;
                const float X = Pop().Scalar;
                PushV({ X, Y, Z, 0.0f });
                break;
            }
            case FormulaOpcode::SwizzleX: PushS(Pop().Vector.X); break;
            case FormulaOpcode::SwizzleY: PushS(Pop().Vector.Y); break;
            case FormulaOpcode::SwizzleZ: PushS(Pop().Vector.Z); break;

            case FormulaOpcode::AddScalar: { const float B = Pop().Scalar; PushS(Pop().Scalar + B); break; }
            case FormulaOpcode::AddVec3:   { const auto  B = Pop().Vector; PushV(Pop().Vector + B); break; }
            case FormulaOpcode::SubScalar: { const float B = Pop().Scalar; PushS(Pop().Scalar - B); break; }
            case FormulaOpcode::SubVec3:   { const auto  B = Pop().Vector; PushV(Pop().Vector - B); break; }
            case FormulaOpcode::MulScalar: { const float B = Pop().Scalar; PushS(Pop().Scalar * B); break; }
            case FormulaOpcode::ScaleVec3:
            {
                const StackSlot Rhs = Pop();
                const StackSlot Lhs = Pop();
                if (Lhs.Category == FormulaExpressionCategory::Scalar && Rhs.Category == FormulaExpressionCategory::Vector3)
                {
                    PushV(Rhs.Vector * Lhs.Scalar);
                }
                else
                {
                    PushV(Lhs.Vector * Rhs.Scalar);
                }
                break;
            }
            case FormulaOpcode::MulVec3Component:
            {
                const auto B = Pop().Vector;
                const auto A = Pop().Vector;
                PushV({ A.X * B.X, A.Y * B.Y, A.Z * B.Z, 0.0f });
                break;
            }
            case FormulaOpcode::DivScalar:
            {
                const float B = Pop().Scalar;
                const float A = Pop().Scalar;
                PushS((std::fabs(B) > 1.0e-6f) ? (A / B) : 0.0f);
                break;
            }
            case FormulaOpcode::DivVec3ByScalar:
            {
                const float B = Pop().Scalar;
                const auto  A = Pop().Vector;
                PushV((std::fabs(B) > 1.0e-6f) ? (A / B) : SpatialVector3{});
                break;
            }
            case FormulaOpcode::PowScalar:
            {
                const float B = Pop().Scalar;
                const float A = Pop().Scalar;
                PushS(std::pow(std::max(0.0f, A), B));
                break;
            }
            case FormulaOpcode::NegateScalar: PushS(-Pop().Scalar); break;
            case FormulaOpcode::NegateVec3:   PushV(Pop().Vector * -1.0f); break;
            case FormulaOpcode::DotVec3:
            {
                const auto B = Pop().Vector;
                const auto A = Pop().Vector;
                PushS(DotProduct(A, B));
                break;
            }
            case FormulaOpcode::CrossVec3:
            {
                const auto B = Pop().Vector;
                const auto A = Pop().Vector;
                PushV(CrossProduct(A, B));
                break;
            }
            case FormulaOpcode::LengthVec3:    PushS(VectorLength(Pop().Vector)); break;
            case FormulaOpcode::NormalizeVec3: PushV(NormalizeSafe(Pop().Vector, {})); break;

            case FormulaOpcode::SinScalar:  PushS(std::sin(Pop().Scalar)); break;
            case FormulaOpcode::CosScalar:  PushS(std::cos(Pop().Scalar)); break;
            case FormulaOpcode::TanScalar:  PushS(std::tan(Pop().Scalar)); break;
            case FormulaOpcode::Atan2Scalar:
            {
                const float X = Pop().Scalar;
                const float Y = Pop().Scalar;
                PushS(std::atan2(Y, X));
                break;
            }
            case FormulaOpcode::SqrtScalar: PushS(std::sqrt(std::max(0.0f, Pop().Scalar))); break;
            case FormulaOpcode::AbsScalar:  PushS(std::fabs(Pop().Scalar)); break;
            case FormulaOpcode::ExpScalar:  PushS(std::exp(std::clamp(Pop().Scalar, -16.0f, 16.0f))); break;
            case FormulaOpcode::MinScalar:  { const float B = Pop().Scalar; PushS(std::min(Pop().Scalar, B)); break; }
            case FormulaOpcode::MaxScalar:  { const float B = Pop().Scalar; PushS(std::max(Pop().Scalar, B)); break; }
            case FormulaOpcode::ClampScalar:
            {
                const float Hi = Pop().Scalar;
                const float Lo = Pop().Scalar;
                const float X  = Pop().Scalar;
                PushS(std::clamp(X, std::min(Lo, Hi), std::max(Lo, Hi)));
                break;
            }
            case FormulaOpcode::SmoothStepScalar:
            {
                const float X  = Pop().Scalar;
                const float E1 = Pop().Scalar;
                const float E0 = Pop().Scalar;
                const float D  = (std::fabs(E1 - E0) > 1.0e-5f) ? ((X - E0) / (E1 - E0)) : 0.0f;
                const float T  = std::clamp(D, 0.0f, 1.0f);
                PushS(T * T * (3.0f - 2.0f * T));
                break;
            }
            case FormulaOpcode::MixScalar:
            {
                const float T = Pop().Scalar;
                const float B = Pop().Scalar;
                const float A = Pop().Scalar;
                PushS(A + (B - A) * T);
                break;
            }
            case FormulaOpcode::MixVec3:
            {
                const float T = Pop().Scalar;
                const auto  B = Pop().Vector;
                const auto  A = Pop().Vector;
                PushV(A + (B - A) * T);
                break;
            }
            case FormulaOpcode::CurlNoiseVec3:
            {
                const float ScaleM = std::max(0.5f, Pop().Scalar);
                const auto  PosVec = Pop().Vector;
                PushV(EvaluateDivergenceFreeCurlPotential(PosVec, ScaleM, 3u, Entry.Seed, TimeSeconds));
                break;
            }
            case FormulaOpcode::SmoothNoiseScalar:
            {
                const auto PosVec = Pop().Vector;
                const float V     = std::sin(PosVec.X * 0.27f + static_cast<float>(TimeSeconds)) *
                                    std::cos(PosVec.Y * 0.31f - PosVec.Z * 0.19f);
                PushS(V);
                break;
            }
            case FormulaOpcode::FlowStreamVec3:
            {
                const float Speed  = Pop().Scalar;
                const auto  DirVec = Pop().Vector;
                (void)Pop(); // Local position consumed
                PushV(NormalizeSafe(DirVec, Basis.Forward) * Speed);
                break;
            }
            case FormulaOpcode::PromoteScalarToDir:
            {
                const float S = Pop().Scalar;
                PushV(Basis.Forward * S);
                break;
            }
        }
    }

    if (Top == 0u)
    {
        return {};
    }
    const StackSlot& Result = Stack[Top - 1u];
    return (Result.Category == FormulaExpressionCategory::Vector3)
               ? Result.Vector
               : (Basis.Forward * Result.Scalar);
}

} // namespace Frontier::SimulationSpace::ForceFields

//============================================================================================================================================
//                                                     MAGNITUDEEXPRESSION.CPP
//============================================================================================================================================
// 🧩 Allocation-free arithmetic expression solver for editable magnitude fields.

#include "SlateUI/Interface/ComponentSpecification/Api/ComponentSpecification.h"

#include <cmath>
#include <cstdlib>

namespace Slate
{
namespace
{

class ExpressionCursor
{
public:
    explicit ExpressionCursor(const char* Incoming) : Current(Incoming != nullptr ? Incoming : "") {}

    double Resolve()
    {
        const double Reading = ResolveSum();
        SkipSpaces();

        if (*Current != '\0' || !std::isfinite(Reading))
            Accepted = false;

        return Reading;
    }

    bool Succeeded() const { return Accepted; }

private:
    void SkipSpaces()
    {
        while (*Current == ' ' || *Current == '\t' || *Current == '\r' || *Current == '\n')
            ++Current;
    }

    bool Take(char Token)
    {
        SkipSpaces();

        if (*Current != Token)
            return false;

        ++Current;
        return true;
    }

    bool TakePowerWord()
    {
        SkipSpaces();

        const bool Matches = (Current[0] == 'e' || Current[0] == 'E')
                          && (Current[1] == 'x' || Current[1] == 'X')
                          && (Current[2] == 'p' || Current[2] == 'P');

        if (!Matches)
            return false;

        Current += 3;
        return true;
    }

    double ResolveSum()
    {
        double Reading = ResolveProduct();

        while (Accepted)
        {
            if (Take('+'))
                Reading += ResolveProduct();
            else if (Take('-'))
                Reading -= ResolveProduct();
            else
                break;
        }

        return Reading;
    }

    double ResolveProduct()
    {
        double Reading = ResolvePower();

        while (Accepted)
        {
            if (Take('*'))
            {
                Reading *= ResolvePower();
            }
            else if (Take('/'))
            {
                const double Divisor = ResolvePower();

                if (Divisor == 0.0)
                {
                    Accepted = false;
                    return 0.0;
                }

                Reading /= Divisor;
            }
            else
            {
                break;
            }
        }

        return Reading;
    }

    double ResolvePower()
    {
        double Reading = ResolveUnary();

        if (Accepted && (Take('^') || TakePowerWord()))
            Reading = std::pow(Reading, ResolvePower());

        if (!std::isfinite(Reading))
            Accepted = false;

        return Reading;
    }

    double ResolveUnary()
    {
        if (Take('+'))
            return ResolveUnary();

        if (Take('-'))
            return -ResolveUnary();

        return ResolvePrimary();
    }

    double ResolvePrimary()
    {
        SkipSpaces();

        if (Take('('))
        {
            const double Reading = ResolveSum();

            if (!Take(')'))
                Accepted = false;

            return Reading;
        }

        char* Ending = nullptr;
        const double Reading = std::strtod(Current, &Ending);

        if (Ending == Current || !std::isfinite(Reading))
        {
            Accepted = false;
            return 0.0;
        }

        Current = Ending;
        return Reading;
    }

    const char* Current = "";   // [-] - next unconsumed byte
    bool        Accepted = true; // [-] - every consumed production remains valid
};

}   // namespace

Deliver<double> ResolveMagnitudeExpression(const char* Expression)
{
    ExpressionCursor Cursor(Expression);
    const double Reading = Cursor.Resolve();

    if (!Cursor.Succeeded())
    {
        return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported,
                                         "the magnitude expression is malformed or non-finite" });
    }

    return Deliver<double>::Result(Reading);
}

}   // namespace Slate

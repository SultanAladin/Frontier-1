//============================================================================================================================================
//                                                   MAGNITUDEEXPRESSIONPROOF.CPP
//============================================================================================================================================
// 🧩 Executable arithmetic grammar proof for reusable editable magnitude fields.

#include "SlateUI/Interface/ComponentSpecification/Api/ComponentSpecification.h"

#include <cmath>
#include <cstdio>

namespace
{

struct ExpressionCase
{
    const char* Run       = "";   // [-] - expression supplied to the solver
    double      Expected  = 0.0;  // [-] - expected finite reading
    bool        Accepted  = true; // [-] - whether resolution should succeed
};

}   // namespace

int main()
{
    const ExpressionCase Cases[] =
    {
        { "10 exp 5", 100000.0, true },
        { "10^5",     100000.0, true },
        { "3.5*320",    1120.0, true },
        { "(2+3)*4",      20.0, true },
        { "2^3^2",       512.0, true },
        { "1/0",           0.0, false },
        { "2+",            0.0, false }
    };

    for (const ExpressionCase& Current : Cases)
    {
        const Slate::Deliver<double> Resolved = Slate::ResolveMagnitudeExpression(Current.Run);

        if (Resolved.Resolved != Current.Accepted ||
            (Resolved.Resolved && std::fabs(Resolved.Resolve() - Current.Expected) > 1.0e-9))
        {
            std::fprintf(stderr, "[FAIL] magnitude expression: %s\n", Current.Run);
            return 1;
        }
    }

    std::puts("[PASS] magnitude expression grammar");
    return 0;
}

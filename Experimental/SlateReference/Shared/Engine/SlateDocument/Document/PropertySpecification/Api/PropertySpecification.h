//============================================================================================================================================
//                                                         PROPERTYSPECIFICATION.H
//============================================================================================================================================
// 🧩 Typed, named, validated property declarations — validation part of the declaration, never a later step.

#pragma once

#include "Foundation/Identity.h"
#include "Foundation/DeliveryGuarantee.h"
#include "SlateMath/Numeric/ColourProjection/Api/ColourProjection.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT IT MEASURES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What a property's value measures, which fixes both its storage and its validation.
/// note  ⚠️ Not a type tag. `SKILL-Naming.md` bans `Role` and `Type` as spellings precisely because they name
///        the category instead of the mechanism; what discriminates here is what the number means.
/// tag   guarantee
enum class PropertyMeasure : std::uint32_t
{
    Truth        = 0u,   // [-] - a declared condition; bounds are not read
    Index      = 1u,   // [-] - an unsigned count or index
    Signed       = 2u,   // [-] - a signed count or displacement
    Magnitude    = 3u,   // [-] - a real quantity, bounded by the declared interval
    Text         = 4u,   // [-] - a name or a declaration the artist typed
    Colour       = 5u,   // [-] - a ColourSpecification; the space is validated, not assumed
    Registration    = 6u,   // [-] - a selection among declared options
    Owner     = 7u,   // [-] - a reference to another owner, generation included
    MeasureCount = 8u    // [-] - the closed count, never a measure
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     ONE VALUE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One property's value, carrying the measure it was declared as.
/// note  🔴 Held as parallel storage rather than a union, so a value read at the wrong measure is a wrong number
///        rather than reinterpreted storage. `10` §2.2's whole point is that an invalid state must not exist
///        between the write and the check.
/// tag   owning
struct PropertyValue
{
    PropertyMeasure      Measured       = PropertyMeasure::Truth;   // [-] - which member below is meaningful
    bool                 TruthDeclared  = false;                    // [-] - read at Truth
    std::uint64_t        IndexHeld    = 0u;                       // [-] - read at Index and Registration
    std::int64_t         SignedHeld     = 0;                        // [-] - read at Signed
    double               MagnitudeHeld  = 0.0;                      // [-] - read at Magnitude
    std::string          TextHeld       = {};                       // [-] - read at Text
    ColourSpecification  ColourHeld     = {};                       // [-] - read at Colour
    OwnerIdentity     OwnerHeld   = {};                       // [-] - read at Owner
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE DECLARATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One property as its owner declared it — its name, its measure, its bounds, and its default.
/// note  🔴 `10` §2.2: validation is part of the declaration and not a separate step performed by whoever writes
///        the value. A property that can hold an invalid value between the write and the check has an invalid
///        state, and something will observe it.
/// note  🔴 `42` §2's rule holds here too: an absent value resolves to the **declared** default, which is not
///        assumed to be zero. A magnitude defaulted to zero produces a surface that is black or invisible, and
///        the artist reads that as a broken material rather than as a missing declaration.
/// note  📝 `76` §4 requires every tool parameter to be one of these, so that `ToolPanel` presents any tool
///        without knowing which. A tool presented by hand-written panel code is a tool the panel must be edited
///        to add.
/// tag   owning
struct PropertyDeclaration
{
    std::string                Identity        = {};                          // [-] - the mechanism's spelling
    std::string                Current       = {};                          // [-] - what the artist reads
    PropertyMeasure            Measured        = PropertyMeasure::Truth;      // [-] - what the value measures
    PropertyValue              Defaulted       = {};                          // [-] - never assumed to be zero
    double                     LowerMagnitude  = 0.0;                         // [-] - read at Magnitude
    double                     UpperMagnitude  = 1.0;                         // [-] - read at Magnitude
    std::int64_t               LowerSigned     = 0;                           // [-] - read at Signed
    std::int64_t               UpperSigned     = 0;                           // [-] - read at Signed
    std::uint64_t              UpperIndex    = 0u;                          // [-] - read at Index; zero unbounds
    std::uint32_t              RequiredSpace   = 0u;                          // [-] - read at Colour; zero accepts any
    std::vector<std::string>   RegisteredOptions = {};                          // [-] - read at Registration
    std::uint32_t              TextExtent      = 0u;                          // [-] - read at Text; zero unbounds
    bool                       BoundsDeclared  = false;                       // [-] - whether the interval is read
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE VALIDATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Whether one value satisfies one declaration.
/// in    Declared  [-]  the declaration, bounds included
/// in    Offered   [-]  the value a caller wishes to write
/// out   Result   [-]  refuses with ContentUnsupported when the measures disagree or a bound is exceeded, and
///                      with IdentityStale when an owner reference is undeclared
/// note  🔴 A refusal names which bound was exceeded, in static text. `86` §4's register presents that text
///        verbatim, so a refusal reading "invalid" sends the artist to guess.
/// cost  ✔️
/// tag   api, nonthrowing
Deliver<bool> Validate(const PropertyDeclaration& Declared, const PropertyValue& Offered);

/// 🧩 Brings one value inside a declaration's bounds where the measure accepts it.
/// in    Declared  [-]  the declaration
/// in    Offered   [-]  the value; returned bounded at Magnitude, Signed and Index
/// out   Result   [-]  refuses when the measures disagree, because no bounding can reconcile that
/// note  🔴 Offered as a **separate** call rather than folded into the write. `10` §2.2 requires the write to
///        refuse, so a write that bounded silently would accept a value the artist can neither see nor correct.
///        Whoever is presenting a slider bounds first and then writes.
/// cost  ✔️
/// tag   api, nonthrowing
Deliver<PropertyValue> Bounded(const PropertyDeclaration& Declared, const PropertyValue& Offered);

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE DECLARATIONS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 An ordered set of declarations and the values held against them.
/// note  🔴 A write validates before it stores, and a rejected write leaves the prior value standing. There is no
///        route by which an invalid value is held here — which is what makes reading one unnecessary to check.
/// tag   owning
class PropertyIndex
{
public:

    /// 🧩 Declares one property, replacing a declaration of the same identity.
    /// in    Declaring  [-]  the declaration
    /// out   Result    [-]  refuses with ContentUnsupported for an empty identity, and when the declaration's
    ///                       own default does not satisfy it
    /// note  🔴 The default is validated against its own declaration here. A declaration whose default is out of
    ///        bounds presents an invalid value on every owner that never wrote it.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Declare(const PropertyDeclaration& Declaring);

    /// 🧩 Writes one property's value, validated first.
    /// in    Identity  [-]  the declaration's spelling
    /// in    Offered   [-]  the value
    /// out   Result   [-]  refuses with ContentUnsupported when nothing declares that identity, and carries
    ///                      Validate's refusal otherwise
    /// post  a rejected write leaves the prior value standing
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Write(const std::string& Identity, const PropertyValue& Offered);

    /// 🧩 Reads one property's value, or its declared default where nothing has written it.
    /// out   Result  [-]  refuses with ContentUnsupported when nothing declares that identity
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<PropertyValue> Resolve(const std::string& Identity) const;

    /// 🧩 One property's declaration, for whoever is presenting it.
    /// out   Result  [-]  refuses with ContentUnsupported when nothing declares that identity
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<PropertyDeclaration> Declared(const std::string& Identity) const;

    /// 🧩 Every declaration, in declaration order.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<PropertyDeclaration>& Declarations() const;

    /// 🧩 Whether one property has been written since it was declared.
    /// cost  🚩
    /// tag   api, nonthrowing
    bool ValueWritten(const std::string& Identity) const;

    /// 🧩 Returns one property to its declared default.
    /// out   Result  [-]  refuses with ContentUnsupported when nothing declares that identity
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Reclaim(const std::string& Identity);

    /// 🧩 🔍 Whether every held value satisfies its own declaration.
    /// note  Structurally true, because Write is the only writer and validates first. Checked so that a future
    ///        second writer is caught by a gate rather than by an artist.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    bool ValuesValid() const;

private:

    std::size_t Located(const std::string& Identity) const;

    std::vector<PropertyDeclaration>  DeclaredProperties;   // [-] - in declaration order
    std::vector<PropertyValue>        HeldValues;           // [-] - parallel to the declarations
    std::vector<bool>                 ValueDeclared;        // [-] - whether a write has landed
};

}   // namespace Slate

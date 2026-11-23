//============================================================================================================================================
//                                                             FACETPANEL.H
//============================================================================================================================================
// 🧩 A reusable multi-facet card — active chips, individual removal, clear-all and a shared selection dropdown.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"
#include "SlateUI/Interface/ComponentSpecification/Api/ComponentSpecification.h"
#include "SlateUI/Interface/ControlIndex/Api/ControlIndex.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/MotionIntegrator/Api/MotionIntegrator.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      FACET GUARANTEE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Names every available facet and optionally gives each one a classification colour.
/// note  Options, colours and enabled ordinates remain owned by the caller. The panel borrows them for one tick.
/// tag   guarantee, nonallocating, nonthrowing
struct FacetDeclaration
{
    const char*         Caption       = "Filters";             // [-] - card heading
    const char* const*  Options       = nullptr;               // [-] - all available facet captions
    const ThemeToken*  Colours          = nullptr;               // [-] - optional classification colours
    std::uint32_t       OptionCount   = 0u;                    // [-] - options and enabled ordinates
    std::uint32_t       LockedIndex = 0xFFFFFFFFu;           // [-] - active facet that cannot be removed
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     FACET PRESENTATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Presents and edits a caller-owned active facet set without retaining application content.
/// tag   owning, nonallocating, nonthrowing
class FacetPanel
{
public:

    static constexpr std::uint32_t FacetCapacity = 24u;   // [-] - bounded available facets
    static constexpr std::uint32_t AbsentFacet   = 0xFFFFFFFFu;

    Deliver<bool> ConstructFacetPanel(MotionIntegrator& Motion,
                            RecordingSurface& Surface,
                            const ThemeProfile& Appearance);
    void Advance(const PointerCondition& Sampled, double Elapsed);
    float MeasureHeight(float Width,
                        const FacetDeclaration& Declared,
                        const bool* Enabled) const;
    Deliver<bool> Record(const PlaneExtent& Extent,
                         const FacetDeclaration& Declared,
                         bool* Enabled);
    void RecordDeferred();
    void Reset();

private:

    struct Arrangement
    {
        PlaneExtent Header       = {};   // [px] - heading and count
        PlaneExtent Chips        = {};   // [px] - wrapped active chips
        PlaneExtent Dropdown     = {};   // [px] - shared selection field
        float       TotalY  = 0.0f; // [px] - complete card height
    };

    Arrangement Arrange(float X,
                        float Y,
                        float Width,
                        const FacetDeclaration& Declared,
                        const bool* Enabled) const;
    bool Pressed(std::uint32_t Index, const PlaneExtent& Extent);
    ThemeToken FacetColour(const FacetDeclaration& Declared, std::uint32_t Index) const;

    MotionIntegrator* Motion = nullptr;
    RecordingSurface* Surface = nullptr;
    const ThemeProfile* Appearance = nullptr;
    ControlIndex Interaction = {};
    ComponentSpecification SharedControls = {};
    ControlIdentity Controls[FacetCapacity + 2u] = {};
    PointerCondition Pointer = {};
    const char* AvailableOptions[FacetCapacity + 1u] = {};
    std::uint32_t AvailableIndexs[FacetCapacity + 1u] = {};
    std::uint32_t AvailableCount = 0u;
    std::uint32_t PendingSelection = 0u;
};

}   // namespace Slate

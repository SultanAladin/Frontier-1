//========================================================================================================================
//                                                  Catalogue.js                                                   🧩
//========================================================================================================================
//
// 📝 The species palette: searchable, grouped by family, and filterable by what a pending connection
//    could accept.
//
//    🔴 Inserting from the catalogue places the new entry at the CENTRE OF THE CURRENT VIEW in tree space,
//       not at a fixed coordinate. Placing at a constant means every insert after a pan lands off-screen
//       and reads as "the button does nothing".

import { ConstructionSpecificationTable } from "../Construction/ConstructionSpecifications.js";
import { CatalogueFamilyOrder, FamilySummary, PortCategories } from "../Construction/PortCategories.js";
import { InsertEntry } from "../Construction/TreeState.js";

export function ComposeCatalogue(Root, Surface, OnInsert)
{
    const Catalogue =
    {
        Root,
        Surface,
        OnInsert,
        Search : Root.querySelector("#CatalogueSearch"),
        Listing: Root.querySelector("#CatalogueListing"),
        Filter : ""
    };

    Catalogue.Search.addEventListener("input", () =>
    {
        Catalogue.Filter = Catalogue.Search.value.trim().toLowerCase();
        RebuildCatalogue(Catalogue);
    });

    RebuildCatalogue(Catalogue);
    return Catalogue;
}

export function RebuildCatalogue(Catalogue)
{
    const Listing = Catalogue.Listing;
    Listing.textContent = "";

    for (const Family of CatalogueFamilyOrder)
    {
        const Species = Object.entries(ConstructionSpecificationTable)
            .filter(([, Specification]) => Specification.Family === Family)
            .filter(([Key, Specification]) => Matches(Catalogue.Filter, Key, Specification));

        if (Species.length === 0) continue;

        const Band = document.createElement("div");
        Band.className = "CatalogueBand";

        const Title = document.createElement("span");
        Title.className = "CatalogueBandNaming";
        Title.textContent = Family;
        Band.appendChild(Title);

        const Hint = document.createElement("span");
        Hint.className = "CatalogueBandHint";
        Hint.textContent = FamilySummary[Family] || "";
        Band.appendChild(Hint);

        Listing.appendChild(Band);

        for (const [Key, Specification] of Species)
        {
            Listing.appendChild(ComposeCatalogueRow(Catalogue, Key, Specification));
        }
    }

    if (Listing.children.length === 0)
    {
        const Empty = document.createElement("div");
        Empty.className = "CatalogueEmpty";
        Empty.textContent = `nothing matches "${Catalogue.Filter}"`;
        Listing.appendChild(Empty);
    }
}

function Matches(Filter, Key, Specification)
{
    if (Filter === "") return true;
    return Key.toLowerCase().includes(Filter)
        || Specification.Naming.toLowerCase().includes(Filter)
        || Specification.Summary.toLowerCase().includes(Filter)
        || Specification.Family.toLowerCase().includes(Filter);
}

function ComposeCatalogueRow(Catalogue, Key, Specification)
{
    const Row = document.createElement("button");
    Row.className = "CatalogueRow";

    // 📝 A singular species already present is shown disabled rather than hidden, so its absence from the
    //    palette is never mistaken for the catalogue being incomplete.
    const AlreadyPresent = Specification.Singular &&
        [...Catalogue.Surface.TreeState.Entries.values()]
            .some(Entry => Entry.Species === Key);

    if (AlreadyPresent) Row.classList.add("Spent");

    const Glyph = document.createElement("span");
    Glyph.className = "CatalogueGlyph";
    Glyph.style.color = PortCategories[Specification.Yields].Swatch;
    Glyph.textContent = Specification.Glyph;
    Row.appendChild(Glyph);

    const Column = document.createElement("span");
    Column.className = "CatalogueText";

    const Naming = document.createElement("span");
    Naming.className = "CatalogueNaming";
    Naming.textContent = Specification.Naming;
    Column.appendChild(Naming);

    const Summary = document.createElement("span");
    Summary.className = "CatalogueSummary";
    Summary.textContent = AlreadyPresent ? "already in the tree" : Specification.Summary;
    Column.appendChild(Summary);

    Row.appendChild(Column);

    if (!AlreadyPresent)
    {
        Row.addEventListener("click", () =>
        {
            const Where = ViewCentreInTreeSpace(Catalogue.Surface);
            const Identifier = InsertEntry(Catalogue.Surface.TreeState, Key, Where);
            Catalogue.OnInsert(Identifier);
        });
    }

    return Row;
}

// 📝 The visible centre, in tree space. Mirrors TreeSurface's ToTreeSpace but for the midpoint rather
//    than a pointer.
function ViewCentreInTreeSpace(Surface)
{
    const Frame = Surface.Host.getBoundingClientRect();
    const Jitter = (Math.random() - 0.5) * 60;                       // avoid perfect stacking on repeats
    return {
        x: Math.round((Frame.width  / 2 - Surface.Pan.x) / Surface.Zoom - 98 + Jitter),
        y: Math.round((Frame.height / 2 - Surface.Pan.y) / Surface.Zoom - 60 + Jitter)
    };
}

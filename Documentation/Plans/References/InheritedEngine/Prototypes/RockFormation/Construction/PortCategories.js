//========================================================================================================================
//                                               PortCategories.js                                                 🧩
//========================================================================================================================
//
// 📝 The five port categories and the rule for what may connect to what.
//
//    🔴 Resistance is a SEPARATE TREE, not a shape input that blends into the surface. This follows
//       Paris 2019: the resistance field psi(p) is sampled by shape operators but never unioned into
//       the distance field. Letting the two mix is what turns stratified rock back into lumpy noise,
//       so the connection rule below enforces the split rather than trusting the author to remember.

export const PortCategories =
{
    Distance:
    {
        Naming  : "Distance",
        Swatch  : "var(--port-distance)",
        Summary : "signed distance to the rock surface — this is what reaches the root"
    },
    Resistance:
    {
        Naming  : "Resistance",
        Swatch  : "var(--port-resistance)",
        Summary : "how hard the rock is at a point [0..1] — sampled by shape, never blended into it"
    },
    Warp:
    {
        Naming  : "Warp",
        Swatch  : "var(--port-warp)",
        Summary : "a displaced probe position — feeds the domain of a downstream evaluation"
    },
    Tint:
    {
        Naming  : "Tint",
        Swatch  : "var(--port-tint)",
        Summary : "linear-space surface colour"
    },
    Scalar:
    {
        Naming  : "Scalar",
        Swatch  : "var(--port-scalar)",
        Summary : "a plain number driving a dial"
    },

    // 📝 Weather is a PROCESS, not a field. Everything above is a function of position evaluated once;
    //    a Weather entry names a compute dispatch that runs repeatedly and changes the grid.
    //
    //    🔴 It yields Weather and takes Weather, forming a CHAIN rather than a tree — thermal then
    //       hydraulic then aeolian, in that order, each reading what the last one left. Order is
    //       meaningful here in a way it never was for the distance tree, where operands commute.
    Weather:
    {
        Naming  : "Weather",
        Swatch  : "var(--port-weather)",
        Summary : "a weathering process applied to the voxel grid — ordered, iterative, destructive"
    }
};

// 📝 Category equality is the whole rule — there is no implicit promotion. A Resistance yield cannot be
//    dropped onto a Distance intake even though both are f32 in the shader, because the two mean
//    different things and the type system is the only thing preventing the confusion.
export function MayConnect(SourceCategory, TargetCategory)
{
    return SourceCategory === TargetCategory;
}

export const CatalogueFamilyOrder =
    ["Mass", "Resistance", "Weather", "Warp", "Combination", "Tint", "Resolve"];

export const FamilySummary =
{
    Mass        : "solid rock bodies — the block of stone before the weather reaches it",
    Resistance  : "differential hardness — the reason one part erodes faster than another",
    Weather     : "erosion processes, run in order over years — this is what makes the shape",
    Warp        : "domain displacement — noise, relief, repetition",
    Combination : "union, carve, repeat — booleans for arches and ridges",
    Tint        : "surface colour from the same fields that drive shape",
    Resolve     : "the root; exactly one per tree"
};

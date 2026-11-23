/*====================================================================================================================================
                                                       CHANNELSET.JS
====================================================================================================================================*/
// 🧩 The six painted channels, and how they pack into three RGBA8 atlases per layer

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

export const ChannelAtlasFormat = "rgba8unorm";

// 🔴 Six channels do NOT mean six textures. Three RGBA8 atlases carry all of them, and the fourth
//    component of each is the per-layer COVERAGE — where this layer has been painted at all. Coverage
//    has to be stored per atlas rather than once globally: a stroke that writes only roughness must
//    leave the colour atlas transparent, or compositing would smear this layer's default grey over
//    every layer beneath it.
//
// 🔴 Normal is absent from this table ON PURPOSE, and it is not an omission. A painted normal is a
//    perturbation of the surface frame, and a brush writing raw RGB into a normal map produces vectors
//    that are neither unit-length nor in tangent space — it would look like coloured noise under any
//    correct shading model. Height IS painted, and the normal is derived from it by finite difference
//    at shade time (see DeriveNormal in SurfaceRasterization). So the user-facing channel count is six;
//    the stored channel count is five.
export const CHANNEL_ATLASES = [
    {
        Key:      "Colour",
        Label:    "Base Colour",
        Channels: ["baseColour"],
        // An unpainted texel in a layer is fully transparent, so the clear is alpha 0 with a mid-grey
        // colour: if a coverage bug ever lets an unpainted texel through, mid-grey is obvious rather
        // than plausible.
        Clear:    [0.5, 0.5, 0.5, 0.0]
    },
    {
        Key:      "Material",
        Label:    "Metallic · Roughness · Height",
        Channels: ["metallic", "roughness", "height"],
        // 📝 Height clears to 0.5, not 0. The channel is SIGNED — 0.5 is the undisplaced surface, below
        //    is a dent and above is a bump. Clearing to 0 would make every unpainted texel a maximum
        //    depression and the derived normal would explode along every island edge.
        Clear:    [0.0, 0.5, 0.5, 0.0]
    },
    {
        Key:      "Emissive",
        Label:    "Emissive",
        Channels: ["emission"],
        Clear:    [0.0, 0.0, 0.0, 0.0]
    }
];

// Which atlas carries a channel, and which component within it.
//
// 📝 Keys match TexturePaintInspector's PBR_CHANNELS exactly ("emission", not "emissive"; "baseColour",
//    not "basecolour"). The inspector's rows are keyed off these strings, so a rename here silently
//    detaches a control from the channel it appears to drive.
export const CHANNEL_SLOTS = {
    baseColour: { Atlas: "Colour",   Component: -1, Kind: "colour", Default: [0.5, 0.5, 0.5] },
    metallic:   { Atlas: "Material", Component:  0, Kind: "scalar", Default: 0.0,  Min: 0, Max: 1 },
    roughness:  { Atlas: "Material", Component:  1, Kind: "scalar", Default: 0.5,  Min: 0, Max: 1 },
    height:     { Atlas: "Material", Component:  2, Kind: "scalar", Default: 0.5,  Min: 0, Max: 1 },
    emission:   { Atlas: "Emissive", Component: -1, Kind: "colour", Default: [0.0, 0.0, 0.0] },

    // 🔴 Derived, never stored. Present in the table so the inspector can show a normal row and so any
    //    code that iterates channels sees it, but Component is null and writing to it is a fault.
    normal:     { Atlas: null,       Component: null, Kind: "derived", Source: "height" },

    // 🔴 NOT a paintable surface channel, and deliberately absent from CHANNEL_ORDER below. A layer mask is
    //    not part of the material — it decides WHERE the material applies — so it must never appear in the
    //    channel list, the channel chips, or the enable/disable pool. It lives here for one reason: it lets
    //    ChannelPreview.Capture render a mask tile through the existing splat-and-readback path, by naming
    //    the atlas key a mask stand-in supplies (see CaptureMask). Anything that walks CHANNEL_ORDER — which
    //    is every consumer that lists channels to the user — will not see it.
    maskCoverage: { Atlas: "maskCoverage", Component: 0, Kind: "scalar", Default: 1.0, Min: 0, Max: 1 }
};

// The order the inspector lists them in, and the order this prototype considers canonical.
export const CHANNEL_ORDER = ["baseColour", "metallic", "roughness", "emission", "normal", "height"];

export const CHANNEL_LABEL = {
    baseColour: "Base Colour",
    metallic:   "Metallic",
    roughness:  "Roughness",
    emission:   "Emissive",
    normal:     "Normal",
    height:     "Height"
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Is this channel actually painted into an atlas, or worked out at shade time?
export function IsStoredChannel(Key)
{
    const Slot = CHANNEL_SLOTS[Key];
    return Boolean(Slot) && Slot.Atlas !== null;
}

// Which atlases a stroke must be laid into, given the channels it is enabled for.
//
// 📝 Returned as a Set of atlas keys rather than a list of channels, because the paint pass runs once
//    per ATLAS, not once per channel: metallic and roughness share one target and one draw.
export function AtlasesForChannels(Keys)
{
    const Targets = new Set();
    for (const Key of Keys)
    {
        const Slot = CHANNEL_SLOTS[Key];
        if (Slot && Slot.Atlas) { Targets.add(Slot.Atlas); }
    }
    return Targets;
}

// Build the write mask and value a dab deposits into one atlas, from the enabled channels and their
// authored values.
//
// 🔴 The mask matters as much as the value. A layer painting only roughness must leave metallic and
//    height untouched where it deposits, so the paint shader lerps per component against this mask
//    rather than writing the whole texel. Without it, dropping a roughness stroke onto a layer that
//    already carries height would flatten the height back to its default wherever the two overlap.
export function ResolveAtlasWrite(AtlasKey, Enabled, Values, Ink)
{
    const Descriptor = CHANNEL_ATLASES.find(A => A.Key === AtlasKey);
    if (!Descriptor) { return null; }

    const Value = [0, 0, 0];
    const Mask  = [0, 0, 0];

    for (const Key of Descriptor.Channels)
    {
        if (!Enabled.has(Key)) { continue; }

        const Slot = CHANNEL_SLOTS[Key];

        if (Slot.Kind === "colour")
        {
            // 📝 Base colour takes the BRUSH ink, so the swatch in the tool menu drives it directly;
            //    every other channel takes the value authored on the layer's channel row. Emissive
            //    takes its own authored colour, not the brush ink — an emissive layer is a property of
            //    the layer, not of whichever swatch happens to be selected.
            //
            // 🔴 When there is no ink, base colour falls back to the LAYER's authored colour, not to the
            //    slot default. A flood (or any deposit with no brush behind it) passes Ink = null, and
            //    resolving that to the mid-grey default silently discards the layer's own colour — a navy
            //    car-paint base floods grey, and because grey is a plausible-looking surface the mistake
            //    reads as "the material layer does nothing" rather than as a wrong value.
            const Authored = Values[Key] ?? Slot.Default;
            const Source = (Key === "baseColour") ? (Ink ?? Authored) : Authored;
            Value[0] = Source[0]; Value[1] = Source[1]; Value[2] = Source[2];
            Mask[0]  = 1;         Mask[1]  = 1;         Mask[2]  = 1;
            continue;
        }

        const Scalar = Values[Key] ?? Slot.Default;
        Value[Slot.Component] = Scalar;
        Mask[Slot.Component]  = 1;
    }

    // Nothing enabled for this atlas means no draw at all, not a draw that writes zeros.
    if (Mask[0] === 0 && Mask[1] === 0 && Mask[2] === 0) { return null; }

    return { Value, Mask, Clear: Descriptor.Clear };
}

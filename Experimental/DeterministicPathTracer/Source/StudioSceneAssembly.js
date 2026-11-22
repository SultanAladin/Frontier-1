/*====================================================================================================================================
                                                     STUDIOSCENEASSEMBLY.JS
====================================================================================================================================*/
// 🧩 Product-studio scene: material table, analytic material spheres, rectangular softbox lights and GPU packing

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

export const SurfaceCategory = Object.freeze({
    Layered:         0,   // Lambert diffuse under a GGX dielectric specular, optional GGX clear coat on top
    Conductor:       1,   // GGX microfacet metal with exact complex-IOR Fresnel
    SolidDielectric: 2,   // Closed refracting volume (Fresnel split into reflection + refraction, Beer absorption)
    ThinDielectric:  3    // Single sheet (two-interface Fresnel, straight transmission, tinted)
});

export const MaterialFloatStride = 24;   // [-] - Six vec4 per material on the GPU
export const SphereFloatStride   = 8;    // [-] - Two vec4 per sphere: centre + radius, material slot
export const LightFloatStride    = 16;   // [-] - Four vec4 per rectangle light

// 📝 Complex refractive indices (η, κ) at roughly 650 / 550 / 450 nm, the usual RGB tabulation of measured metals.
const MeasuredConductors = Object.freeze({
    Gold:      { Eta: [0.143, 0.374, 1.442], Kappa: [3.983, 2.385, 1.603] },
    Copper:    { Eta: [0.200, 0.924, 1.102], Kappa: [3.912, 2.452, 2.142] },
    Aluminium: { Eta: [1.657, 0.880, 0.521], Kappa: [9.224, 6.270, 4.837] },
    Chromium:  { Eta: [3.105, 3.181, 2.330], Kappa: [3.330, 3.330, 3.150] },
    Silver:    { Eta: [0.155, 0.117, 0.138], Kappa: [4.828, 3.122, 2.147] }
});

// 📝 Body coat presets. "Authored" is DriveSceneAuthor's MetalicCoat (0.680005, 0.800458, 0) × 0.78 flake headroom.
export const PaintPresets = Object.freeze({
    Authored:  { Label: 'Authored lime',  Albedo: [0.680005 * 0.78, 0.800458 * 0.78, 0.0] },
    Racing:    { Label: 'Racing red',     Albedo: [0.62, 0.025, 0.018] },
    Midnight:  { Label: 'Midnight blue',  Albedo: [0.012, 0.035, 0.16] },
    Pearl:     { Label: 'Pearl white',    Albedo: [0.80, 0.79, 0.76] },
    Graphite:  { Label: 'Graphite',       Albedo: [0.035, 0.037, 0.040] }
});

export const FloorPresets = Object.freeze({
    Epoxy:    { Label: 'Polished epoxy', Albedo: [0.16, 0.16, 0.17], Roughness: 0.12, CoatWeight: 1.0, CoatRoughness: 0.035 },
    Concrete: { Label: 'Matte concrete', Albedo: [0.30, 0.29, 0.28], Roughness: 0.70, CoatWeight: 0.0, CoatRoughness: 0.0 },
    Mirror:   { Label: 'Black mirror',   Albedo: [0.01, 0.01, 0.01], Roughness: 0.03, CoatWeight: 0.0, CoatRoughness: 0.0 },
    Lambert:  { Label: 'Lambert 50 % (reference)', Albedo: [0.5, 0.5, 0.5], Roughness: 1.0, CoatWeight: 0.0, CoatRoughness: 0.0, SpecularF0: 0.0 }
});

export const EnvironmentPresets = Object.freeze({
    Studio:   { Label: 'Dark studio',   Category: 0 },
    Overcast: { Label: 'Overcast sky',  Category: 1 },
    Dusk:     { Label: 'Warm dusk',     Category: 2 }
});


//------------------------------------------------------------------------------------------------------------------------
//                                                    MATERIAL CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

function LayeredSurface(Albedo, Roughness, CoatWeight = 0.0, CoatRoughness = 0.0, Exitance = [0, 0, 0])
{
    return { Category: SurfaceCategory.Layered, Albedo, Roughness, CoatWeight, CoatRoughness, Exitance,
             Eta: [1, 1, 1], Kappa: [0, 0, 0], Absorption: [0, 0, 0], Ior: 1.5, SpecularF0: 0.04 };
}

function ConductorSurface(Conductor, Roughness)
{
    return { Category: SurfaceCategory.Conductor, Albedo: [1, 1, 1], Roughness, CoatWeight: 0.0, CoatRoughness: 0.0,
             Exitance: [0, 0, 0], Eta: Conductor.Eta, Kappa: Conductor.Kappa, Absorption: [0, 0, 0], Ior: 1.5, SpecularF0: 0.04 };
}

function SolidDielectricSurface(Ior, Absorption)
{
    return { Category: SurfaceCategory.SolidDielectric, Albedo: [1, 1, 1], Roughness: 0.0, CoatWeight: 0.0, CoatRoughness: 0.0,
             Exitance: [0, 0, 0], Eta: [1, 1, 1], Kappa: [0, 0, 0], Absorption, Ior, SpecularF0: 0.04 };
}

function ThinDielectricSurface(Ior, Tint)
{
    return { Category: SurfaceCategory.ThinDielectric, Albedo: Tint, Roughness: 0.0, CoatWeight: 0.0, CoatRoughness: 0.0,
             Exitance: [0, 0, 0], Eta: [1, 1, 1], Kappa: [0, 0, 0], Absorption: [0, 0, 0], Ior, SpecularF0: 0.04 };
}

// 📝 Slot order is the GPU material index. Vehicle slots follow MaterialSpecification.js (VehicleProvingGrounds) values;
//    showcase slots demonstrate each transport category on analytic spheres.
export function ConstructMaterialTable(Options)
{
    const Paint = PaintPresets[Options.PaintPreset] || PaintPresets.Authored;
    const Floor = FloorPresets[Options.FloorPreset] || FloorPresets.Epoxy;
    // 📝 Lamp lenses are emissive triangles reached only by BSDF rays (not light-sampled), so the authored 48.7
    //    radiance is scaled down by default to keep their floor pool from dominating early samples.
    const LampScale = Options.LampExitance ?? 0.2;
    const Slots = [
        ['BodyCoat',       LayeredSurface(Paint.Albedo, Options.PaintRoughness, 1.0, 0.03)],
        ['VehicleGlass',   ThinDielectricSurface(1.52, [0.28, 0.30, 0.31])],
        ['VehiclePlastic', LayeredSurface([0.012, 0.014, 0.018], 0.50)],
        ['TyreRubber',     LayeredSurface([0.03741, 0.03741, 0.03741], 0.76087)],
        ['HubAccent',      ConductorSurface({ Eta: [1.10, 0.95, 0.55], Kappa: [3.2, 3.6, 4.9] }, 0.22)],
        ['BrakeDisc',      ConductorSurface(MeasuredConductors.Chromium, 0.29)],
        ['VehicleTrim',    LayeredSurface([0.8, 0.8, 0.8], 0.50)],
        ['Headlight',      LayeredSurface([0.8, 0.8, 0.8], 0.10, 0.0, 0.0, [48.699997, 48.699997, 48.699997].map(C => C * LampScale))],
        ['Taillight',      LayeredSurface([0.8, 0.8, 0.8], 0.10, 0.0, 0.0, [6.5, 0.0, 6.5 * 0.016442].map(C => C * Math.max(LampScale, 0.5)))],
        ['RimAlloy',       ConductorSurface(MeasuredConductors.Aluminium, 0.30)],
        ['StudioFloor',    { ...LayeredSurface(Floor.Albedo, Floor.Roughness, Floor.CoatWeight, Floor.CoatRoughness), SpecularF0: Floor.SpecularF0 ?? 0.04 }],
        ['PolishedGold',   ConductorSurface(MeasuredConductors.Gold, 0.18)],
        ['BrushedCopper',  ConductorSurface(MeasuredConductors.Copper, 0.38)],
        ['MirrorChrome',   ConductorSurface(MeasuredConductors.Silver, 0.0)],
        ['ClearGlass',     SolidDielectricSurface(1.5, [0.02, 0.02, 0.02])],
        ['EmeraldGlass',   SolidDielectricSurface(1.57, [2.6, 0.35, 1.9])],
        ['GlazedCeramic',  LayeredSurface([0.72, 0.07, 0.05], 0.35, 1.0, 0.02)],
        ['MatteChalk',     LayeredSurface([0.82, 0.82, 0.80], 0.90)],
        ['CanopyGloss',    LayeredSurface([0.03741, 0.03741, 0.03741], 0.35, 1.0, 0.03)],
        // 📝 Every parked vehicle gets its own body finish and one of four wheel finishes (ResolveParkingBays).
        ...ConstructFleetFinishes(ParkingCapacity - 1).map((Surface, Index) => [`FleetFinish${Index}`, Surface]),
        ['FleetRim0',      ConductorSurface(MeasuredConductors.Chromium, 0.06)],
        ['FleetRim1',      LayeredSurface([0.02, 0.02, 0.022], 0.30, 1.0, 0.04)],
        ['FleetRim2',      ConductorSurface(MeasuredConductors.Gold, 0.24)],
        ['FleetRim3',      ConductorSurface(MeasuredConductors.Aluminium, 0.45)]
    ];
    const SlotIndex = Object.fromEntries(Slots.map(([Label], Index) => [Label, Index]));
    return { Slots, SlotIndex };
}

// 📝 Artist-friendly metallic Fresnel (Gulbrandsen 2014): reflectivity r (normal-incidence colour) and edge tint g
//    → per-channel complex IOR (η, κ) for the exact conductor Fresnel.
function TintedConductor(Reflectivity, EdgeTint)
{
    const Eta = [], Kappa = [];
    for (let Channel = 0; Channel < 3; Channel++)
    {
        const R = Math.min(Math.max(Reflectivity[Channel], 0.001), 0.99), G = EdgeTint[Channel];
        const Minimum = (1 - R) / (1 + R), Maximum = (1 + Math.sqrt(R)) / (1 - Math.sqrt(R));
        const N = G * Minimum + (1 - G) * Maximum;
        Eta.push(N);
        Kappa.push(Math.sqrt(Math.max(0, (R * (N + 1) ** 2 - (N - 1) ** 2) / (1 - R))));
    }
    return { Eta, Kappa };
}

function HueColour(Hue, Saturation, Value)
{
    const Channel = Offset => { const K = (Offset + Hue * 6) % 6; return Value * (1 - Saturation * Math.max(0, Math.min(K, 4 - K, 1))); };
    return [Channel(5), Channel(3), Channel(1)];
}

// 📝 Fixed rule, no RNG: hues step by the golden ratio (never repeating), finishes cycle with a coprime stride through
//    six real paint families so neighbours differ in both colour and finish.
const FleetFinishFamilies = ['Gloss', 'Metallic', 'Matte', 'Satin metal', 'Pearl', 'Candy chrome'];
export function ConstructFleetFinishes(Count)
{
    const Finishes = [];
    for (let Index = 0; Index < Count; Index++)
    {
        const Hue = (0.08 + Index * 0.6180339887) % 1;
        const Family = FleetFinishFamilies[(Index * 5) % FleetFinishFamilies.length];
        const Dark = Index % 7 === 3, Light = Index % 7 === 5;
        const Colour = Light ? HueColour(Hue, 0.12, 0.80) : HueColour(Hue, Dark ? 0.55 : 0.88, Dark ? 0.07 : 0.55);
        let Surface;
        if (Family === 'Gloss')        Surface = LayeredSurface(Colour, 0.30, 1.0, 0.025);
        else if (Family === 'Metallic') Surface = { ...LayeredSurface(Colour.map(C => C * 0.6), 0.16, 1.0, 0.03), SpecularF0: 0.22 };
        else if (Family === 'Matte')   Surface = LayeredSurface(Colour, 0.62);
        else if (Family === 'Satin metal') Surface = ConductorSurface(TintedConductor(Colour.map(C => 0.25 + 0.7 * C), Colour.map(C => 0.5 + 0.5 * C)), 0.32);
        else if (Family === 'Pearl')   Surface = { ...LayeredSurface(HueColour(Hue, 0.10, 0.82), 0.22, 1.0, 0.02), SpecularF0: 0.07 };
        else                           Surface = ConductorSurface(TintedConductor(Colour.map(C => 0.15 + 0.8 * C), [0.9, 0.9, 0.9]), 0.07);
        Finishes.push({ ...Surface, Family });
    }
    return Finishes;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    VEHICLE PARKING
//------------------------------------------------------------------------------------------------------------------------

const FleetRimCount   = 4;
// 📝 Assembled vehicle bounds: X ±3.11 m (length 6.22), Z ±1.25 m (width 2.5), height 1.77 m. Cars park side by side
//    along Z with a 1.1 m door gap; rows along X are separated by a 3.5 m aisle. Bays extend to −Z, away from the
//    material spheres on the +Z side. 5 rows × 10 bays = 50 vehicles (1,253,400 triangles).
const ParkingRowPitch = 9.7;    // [m] - X: 6.22 m body + 3.5 m aisle
const ParkingBayPitch = 3.6;    // [m] - Z: 2.5 m body + 1.1 m gap
const ParkingRows     = [0, -1, 1, -2, 2];
const ParkingBays     = 10;
export const ParkingCapacity = ParkingRows.length * ParkingBays;

// 📝 Bay 0 is the hero vehicle at the origin. Further bays fill outward by distance; alternate rows face the other
//    way (nose to the aisle). Returns { X, Z, Yaw, PaintSlot, RimSlot }; null keeps the hero's BodyCoat / RimAlloy.
export function ResolveParkingBays(VehicleCount)
{
    const Bays = [];
    for (const Row of ParkingRows)
    {
        for (let Bay = 0; Bay < ParkingBays; Bay++) Bays.push({ X: Row * ParkingRowPitch, Z: -Bay * ParkingBayPitch, Yaw: Math.abs(Row) % 2 ? Math.PI : 0 });
    }
    Bays.sort((Left, Right) => (Math.hypot(Left.X, Left.Z * 2.0) - Math.hypot(Right.X, Right.Z * 2.0)) || (Left.X - Right.X) || (Right.Z - Left.Z));
    return Bays.slice(0, Math.max(1, Math.min(VehicleCount, Bays.length)))
               .map((Bay, Index) => ({ ...Bay, PaintSlot: Index === 0 ? null : `FleetFinish${Index - 1}`,
                                       RimSlot: Index === 0 ? null : `FleetRim${(Index * 3) % FleetRimCount}` }));
}

export function PackMaterialTable(MaterialTable)
{
    const Packed = new Float32Array(MaterialTable.Slots.length * MaterialFloatStride);
    MaterialTable.Slots.forEach(([, Surface], Index) =>
    {
        const O = Index * MaterialFloatStride;
        Packed.set([...Surface.Albedo, Surface.Roughness], O);
        Packed.set([...Surface.Eta, Surface.Category], O + 4);
        Packed.set([...Surface.Kappa, Surface.CoatWeight], O + 8);
        Packed.set([...Surface.Exitance, Surface.CoatRoughness], O + 12);
        Packed.set([...Surface.Absorption, Surface.Ior], O + 16);
        Packed.set([Surface.SpecularF0, 0, 0, 0], O + 20);
    });
    return Packed;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    ANALYTIC PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Material spheres rest on the floor in an arc in front of the vehicle's right flank (studio +Z side).
export function ConstructShowcaseSpheres(SlotIndex)
{
    const Radius = 0.46;
    const Layout = ['PolishedGold', 'BrushedCopper', 'MirrorChrome', 'ClearGlass', 'EmeraldGlass', 'GlazedCeramic', 'MatteChalk'];
    const Packed = new Float32Array(Layout.length * SphereFloatStride);
    let MinX = Infinity, MinY = Infinity, MinZ = Infinity, MaxX = -Infinity, MaxY = -Infinity, MaxZ = -Infinity;
    Layout.forEach((Label, Index) =>
    {
        const ArcParameter = (Index - (Layout.length - 1) / 2) / ((Layout.length - 1) / 2);   // [-1, 1]
        const X = ArcParameter * 3.7;
        const Z = 2.75 + 0.55 * ArcParameter * ArcParameter;
        Packed.set([X, Radius, Z, Radius, SlotIndex[Label], 0, 0, 0], Index * SphereFloatStride);
        MinX = Math.min(MinX, X - Radius); MaxX = Math.max(MaxX, X + Radius);
        MinY = Math.min(MinY, 0);          MaxY = Math.max(MaxY, Radius * 2);
        MinZ = Math.min(MinZ, Z - Radius); MaxZ = Math.max(MaxZ, Z + Radius);
    });
    const Cx = 0.5 * (MinX + MaxX), Cy = 0.5 * (MinY + MaxY), Cz = 0.5 * (MinZ + MaxZ);
    let BoundRadius = 0;
    for (let Index = 0; Index < Layout.length; Index++)
    {
        const O = Index * SphereFloatStride;
        BoundRadius = Math.max(BoundRadius, Math.hypot(Packed[O] - Cx, Packed[O + 1] - Cy, Packed[O + 2] - Cz) + Packed[O + 3]);
    }
    return { Packed, Count: Layout.length, Labels: Layout, BoundsSphere: [Cx, Cy, Cz, BoundRadius + 1e-3] };
}

// 📝 A rectangle of Width × Height centred at Centre whose emitting face looks at AimPoint. EdgeU is horizontal, and
//    cross(EdgeU, EdgeV) equals the emitting normal, matching LightNormal() in RadianceIntegration.wgsl.
function AimedRectangle(Centre, AimPoint, Width, Height)
{
    const Normalize = V => { const L = Math.hypot(V[0], V[1], V[2]) || 1; return V.map(C => C / L); };
    const Cross = (A, B) => [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]];
    const Normal  = Normalize(AimPoint.map((C, Axis) => C - Centre[Axis]));
    const Lateral = Math.abs(Normal[1]) > 0.99 ? [1, 0, 0] : Normalize(Cross([0, 1, 0], Normal));
    const Upright = Cross(Normal, Lateral);
    return { Centre, EdgeU: Lateral.map(C => C * Width), EdgeV: Upright.map(C => C * Height) };
}

// 📝 Three-point product rig: a large overhead key, a long rim strip behind the vehicle and a soft fill panel raised
//    above the hero camera (outside its frustum). Radiance is in linear scene units.
export function ConstructSoftboxLights(Options)
{
    const Lights = [
        { ...AimedRectangle([0.4, 5.8, 0.6],  [0.4, 0.0, 0.6],  5.6, 2.6), Radiance: [1.00, 0.98, 0.95], Scale: Options.KeyIntensity },
        { ...AimedRectangle([-1.5, 2.6, -6.0], [0.0, 0.8, 0.0], 7.0, 0.8), Radiance: [0.92, 0.96, 1.00], Scale: Options.RimIntensity },
        { ...AimedRectangle([5.8, 6.2, 6.4],  [0.0, 0.6, 0.6],  3.2, 2.2), Radiance: [1.00, 0.93, 0.85], Scale: Options.FillIntensity }
    ].filter(Light => Light.Scale > 0.0);

    const Packed = new Float32Array(Math.max(1, Lights.length) * LightFloatStride);
    Lights.forEach((Light, Index) =>
    {
        const [Ux, Uy, Uz] = Light.EdgeU, [Vx, Vy, Vz] = Light.EdgeV;
        const Cx = Uy * Vz - Uz * Vy, Cy = Uz * Vx - Ux * Vz, Cz = Ux * Vy - Uy * Vx;
        const Area = Math.hypot(Cx, Cy, Cz);
        const InvArea = Area > 0 ? 1 / Area : 0;
        // 📝 Pack precomputed unit normal in EdgeU.w, EdgeV.w, Radiance.w so the shader skips cross+normalize per ray.
        Packed.set([...Light.Centre, Area, ...Light.EdgeU, Cx * InvArea, ...Light.EdgeV, Cy * InvArea,
                    ...Light.Radiance.map(Channel => Channel * Light.Scale), Cz * InvArea], Index * LightFloatStride);
    });
    return { Packed, Count: Lights.length };
}

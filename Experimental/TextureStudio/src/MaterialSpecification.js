//============================================================================================================================================
//                                                          MATERIALSPECIFICATION.JS                                                          
//============================================================================================================================================
// 🧩 Separate PBR-layer addresses, procedural-field inputs and designer projections over shared material parameters.

import { normalizeTextureSource } from "./textureSource.js";
import { GeneratorDefinitions, CamouflagePalette } from "./FieldSpecification.js";
import { getRecipe } from "./materialProfiles.js";
export const AuthoringIdentity = () => globalThis.crypto.randomUUID();
export const PbrChannels = Object.freeze({
    colour: { Caption: "Colour", Type: "colour", Target: "color", Default: "#ffffff" },
    roughness: { Caption: "Roughness", Type: "scalar", Target: "roughness", Default: .5, Minimum: 0, Maximum: 1 },
    metallic: { Caption: "Metallic", Type: "scalar", Target: "metalness", Minimum: 0, Maximum: 1 },
    normal: { Caption: "Normal", Type: "vector", Target: "normal", Default: [0, 0, 1] },
    microNormal: { Caption: "Micro normal", Type: "vector", Target: "microNormal", Default: [0, 0, 1] },
    ior: { Caption: "IOR", Type: "scalar", Target: "ior", Default: 1.5, Minimum: 1, Maximum: 3 },
    specular: { Caption: "Specular", Type: "scalar", Target: "specular", Default: 1, Minimum: 0, Maximum: 1 },
    thickness: { Caption: "Thickness", Type: "scalar", Target: "depth", Minimum: 0, Maximum: 10 },
    transmission: { Caption: "Transmission", Type: "scalar", Target: "translucency", Minimum: 0, Maximum: 1 },
    coverage: { Caption: "Coverage", Type: "scalar", Target: "coverage", Default: 1, Minimum: 0, Maximum: 1 },
    sheen: { Caption: "Sheen", Type: "scalar", Target: "sheen", Minimum: 0, Maximum: 1 },
    sheenRoughness: { Caption: "Sheen roughness", Type: "scalar", Target: "sheenRoughness", Minimum: 0, Maximum: 1 },
    anisotropy: { Caption: "Anisotropy", Type: "scalar", Target: "anisotropy", Minimum: 0, Maximum: 1 }
});
export function ChannelAddress(Layer, Channel)
{
    if (Layer.Model === "coat") return ({ roughness: "coatRoughness", ior: "coatIor", coverage: "coat", normal: "coatNormal" })[Channel] || `layer:${Layer.Identity}:${Channel}`;
    if (Layer.Model === "flakes") return `flake-output:${Channel}`;
    if (Layer.Model === "surface") return PbrChannels[Channel].Target;
    return `layer:${Layer.Identity}:${Channel}`;
}
const Physical = (Channel, Quantity) =>
{
    const Specification = PbrChannels[Channel];
    return Specification.Type === "scalar" ? Math.max(Specification.Minimum, Math.min(Specification.Maximum, Number(Quantity))) : Quantity;
};
function GeneratorDeclarations(Parameters)
{
    const Recipe = getRecipe(Parameters), Entries = [];
    if (Parameters.type === 0) Entries.push({ Identity: AuthoringIdentity(), Name: "Flake distribution", Category: "Particle fields", Procedure: "flakes", Enabled: true,
        Inputs: ["flakes", "flakeSize", "flakeScale", "flakeLayers", "flakeLayerDepth", "flakeTilt", "flakeRoughnessMin", "flakeRoughnessMax", "flakeMetalnessMin", "flakeMetalnessMax"], Outputs: ["colour", "roughness", "metallic", "normal", "coverage"] });
    const Procedure = ({ 17: "solar", 18: "dimples", 29: "scratches" })[Parameters.type] || (Recipe.woven ? "weave" : Recipe.id);
    if (Parameters.type !== 0) Entries.push({ Identity: AuthoringIdentity(), Name: ({ solar: "Solar cell grid", dimples: "Dimple distribution", scratches: "Scratch field", weave: "Yarn construction" })[Procedure] || Recipe.title,
        Category: Procedure === "weave" ? "Textile fields" : ["dimples", "scratches"].includes(Procedure) ? "Relief fields" : "Structural fields", Procedure, Enabled: true,
        Inputs: [...new Set(Recipe.controls.flatMap(Control => Control.direct ? [Control.key] : (Control.targets || []).map(Target => Target.key)).filter(Key => !Object.values(PbrChannels).some(Channel => Channel.Target === Key) && !["coat", "coatRoughness", "coatIor"].includes(Key)))], Outputs: ["colour", "normal", "roughness", "metallic"] });
    if (Parameters.metalScratches && Parameters.type !== 29) Entries.push({ Identity: AuthoringIdentity(), Name: "Scratch field", Category: "Relief fields", Procedure: "scratches", Enabled: true,
        Inputs: ["scratchAngle", "scratchDensity", "scratchLength", "scratchWidth", "scratchDepth", "scratchSpread", "scratchBend", "scratchScale"], Outputs: ["normal", "roughness", "colour"] });
    return Entries;
}
export function DeclareMaterialAuthoring(Parameters)
{
    const Generators = GeneratorDeclarations(Parameters);
    const Surface = { Identity: AuthoringIdentity(), Name: Parameters.type === 0 ? "Paint body" : "Surface body", Model: "surface", Enabled: true,
        Channels: ["colour", "roughness", "metallic", "normal", "ior", "specular", "thickness", ...(Parameters.type === 5 || Parameters.translucency > 0 ? ["transmission"] : []), ...(Parameters.sheen > 0 ? ["sheen", "sheenRoughness"] : []), ...(Parameters.anisotropy > 0 ? ["anisotropy"] : [])] };
    const Layers = [Surface];
    if (Parameters.type === 0) Layers.unshift({ Identity: AuthoringIdentity(), Name: "Metallic flakes", Model: "flakes", Generator: Generators.find(Generator => Generator.Procedure === "flakes").Identity, Enabled: true, Channels: ["colour", "roughness", "metallic", "normal", "coverage"] });
    if (Parameters.type === 0 || Parameters.coat > 0) Layers.unshift({ Identity: AuthoringIdentity(), Name: "Clearcoat", Model: "coat", Enabled: true, Channels: ["coverage", "roughness", "ior"] });
    return { Version: 2, Layers, Generators, Variables: [], Links: {}, Inputs: {}, Sources: {}, Designer: {} };
}
export function NormalizeMaterialAuthoring(Input)
{
    if (!Input) return undefined;
    const Result = structuredClone(Input);
    if (Result.Version !== 2 || !Array.isArray(Result.Layers) || Result.Layers.length > 16 || !Result.Layers.length || !Array.isArray(Result.Generators) || Result.Generators.length > 32 || !Array.isArray(Result.Variables) || Result.Variables.length > 128) throw Error("Invalid material authoring document.");
    const Tokens = new Set(), Names = new Set();
    const Token = Identity => { if (typeof Identity !== "string" || !/^[a-f\d-]{36}$/i.test(Identity) || Tokens.has(Identity)) throw Error("Invalid or duplicate authoring identity."); Tokens.add(Identity); };
    for (const Layer of Result.Layers)
    {
        Token(Layer.Identity);
        if (typeof Layer.Name !== "string" || Layer.Name.length > 80 || !["surface", "flakes", "coat", "detail"].includes(Layer.Model) || typeof Layer.Enabled !== "boolean" || !Array.isArray(Layer.Channels) || new Set(Layer.Channels).size !== Layer.Channels.length || Layer.Channels.some(Channel => !Object.hasOwn(PbrChannels, Channel))) throw Error("Invalid PBR layer.");
    }
    for (const Generator of Result.Generators)
    {
        Token(Generator.Identity);
        if (typeof Generator.Name !== "string" || Generator.Name.length > 80 || typeof Generator.Category !== "string" || Generator.Category.length > 60 || typeof Generator.Enabled !== "boolean" || !Array.isArray(Generator.Inputs) || Generator.Inputs.length > 100 || Generator.Inputs.some(Address => typeof Address !== "string" || Address.length > 150)) throw Error("Invalid field declaration.");
    }
    for (const Variable of Result.Variables)
    {
        Token(Variable.Identity);
        if (typeof Variable.Name !== "string" || !Variable.Name.trim() || Variable.Name.length > 60 || Names.has(Variable.Name) || typeof Variable.Address !== "string" || Variable.Address.length > 150 || !["scalar", "colour", "vector"].includes(Variable.Type)) throw Error("Invalid shared variable.");
        Names.add(Variable.Name);
        if (Variable.Type === "scalar" && (!Number.isFinite(Variable.Minimum) || !Number.isFinite(Variable.Maximum) || Variable.Minimum >= Variable.Maximum)) throw Error("Invalid variable range.");
        if (Variable.Exposure && (typeof Variable.Exposure.Name !== "string" || !Variable.Exposure.Name.trim() || Variable.Exposure.Name.length > 80 || (Variable.Type === "scalar" && (!Number.isFinite(Variable.Exposure.Minimum) || !Number.isFinite(Variable.Exposure.Maximum) || Variable.Exposure.Minimum < Variable.Minimum || Variable.Exposure.Maximum > Variable.Maximum || Variable.Exposure.Minimum >= Variable.Exposure.Maximum)))) throw Error("Invalid designer exposure.");
    }
    for (const [Address, Reference] of Object.entries(Result.Links || {}))
    {
        if (Address.length > 150 || !Result.Variables.some(Variable => Variable.Identity === Reference)) throw Error("Missing shared source.");
        ResolveAddress({ Authoring: Result }, Address);
    }
    for (const Container of [Result.Inputs, Result.Sources, Result.Designer]) if (!Container || typeof Container !== "object" || Array.isArray(Container) || Object.keys(Container).length > 512) throw Error("Invalid authoring parameter index.");
    const AddressValid = Address => typeof Address === "string" && /^[a-zA-Z0-9:-]{1,150}$/.test(Address) && !["__proto__", "constructor", "prototype"].includes(Address);
    for (const Variable of Result.Variables) ResolveAddress({ Authoring: Result }, Variable.Address);
    for (const Variable of Result.Variables) if (!AddressValid(Variable.Address)) throw Error("Invalid parameter address.");
    for (const Generator of Result.Generators) if (Generator.Inputs.some(Address => !AddressValid(Address)) || (Generator.Coded && !GeneratorDefinitions.some(Entry => Entry.identity === Generator.Procedure))) throw Error("Unknown generator procedure or input.");
    const InputValid = Quantity => typeof Quantity === "boolean" || (typeof Quantity === "number" && Number.isFinite(Quantity)) || (typeof Quantity === "string" && Quantity.length <= 1024) || (Array.isArray(Quantity) && Quantity.length <= 32 && Quantity.every(Component => !Array.isArray(Component) && InputValid(Component)));
    for (const [Address, Quantity] of Object.entries(Result.Inputs)) if (!AddressValid(Address) || !InputValid(Quantity)) throw Error("Invalid field input.");
    for (const [Address, Source] of Object.entries(Result.Sources))
    {
        if (!AddressValid(Address) || !Source || !["value", "solid", "texture", "linear", "radial", "conic", "ramp", "field", "pattern"].includes(Source.Mode)) throw Error("Invalid channel source.");
        if (Source.Direct !== undefined && typeof Source.Direct !== "boolean") throw Error("Invalid direct channel input.");
        if (Source.Texture && !normalizeTextureSource(Source.Texture)) throw Error("Invalid embedded channel image.");
        if (Source.Stops && (!Array.isArray(Source.Stops) || Source.Stops.length < 2 || Source.Stops.length > 8 || Source.Stops.some(Colour => !/^#[a-f\d]{6}$/i.test(Colour)))) throw Error("Invalid channel colour ramp.");
        if (Source.Generator && !Result.Generators.some(Generator => Generator.Identity === Source.Generator && Generator.Coded)) throw Error("Missing coded field instance.");
    }
    for (const [Address, Reference] of Object.entries(Result.Links))
    {
        if (!AddressValid(Address)) throw Error("Invalid destination address.");
        const Variable = Result.Variables.find(Entry => Entry.Identity === Reference);
        const Channel = Address.includes(":") ? PbrChannels[Address.split(":").at(-1)] : Object.values(PbrChannels).find(Entry => Entry.Target === Address) || ({ coat: PbrChannels.coverage, coatRoughness: PbrChannels.roughness, coatIor: PbrChannels.ior, coatNormal: PbrChannels.normal })[Address];
        if (Channel && (Variable.Type !== Channel.Type || (Channel.Type === "scalar" && (Variable.Minimum < Channel.Minimum || Variable.Maximum > Channel.Maximum)))) throw Error("Shared variable does not fit this channel domain.");
    }
    return Result;
}
export function ResolveAddress(Parameters, Address, Visited = new Set())
{
    if (Visited.has(Address)) throw Error("Cyclic shared material variable.");
    Visited.add(Address);
    const Reference = Parameters.Authoring?.Links[Address];
    if (!Reference) return Address;
    const Variable = Parameters.Authoring.Variables.find(Entry => Entry.Identity === Reference);
    if (!Variable) throw Error("Shared variable is missing.");
    return ResolveAddress(Parameters, Variable.Address, Visited);
}
export function ReadMaterialParameter(Parameters, Address)
{
    const Resolved = ResolveAddress(Parameters, Address);
    return Resolved.includes(":") ? Parameters.Authoring?.Inputs[Resolved] : Parameters[Resolved];
}
export function WriteMaterialParameter(Parameters, Address, Quantity)
{
    if (typeof Quantity === "number" && !Number.isFinite(Quantity)) throw Error("Material input must be finite.");
    const Result = structuredClone(Parameters), Resolved = ResolveAddress(Result, Address);
    if (Resolved.includes(":")) Result.Authoring.Inputs[Resolved] = Quantity;
    else Result[Resolved] = Quantity;
    return Result;
}
export function WritePbrChannel(Parameters, Layer, Channel, Quantity)
{
    return WriteMaterialParameter(Parameters, ChannelAddress(Layer, Channel), Physical(Channel, Quantity));
}
export function DesignerControlQuantity(Parameters, Control)
{
    if (Control.direct) return ReadMaterialParameter(Parameters, Control.key) ?? Control.min;
    const Target = Control.targets[0], Quantity = ReadMaterialParameter(Parameters, Target.key) ?? Target.min;
    return Math.max(0, Math.min(1, (Quantity - Target.min) / (Target.max - Target.min)));
}
export function ProjectDesignerControl(Parameters, Control, Quantity)
{
    if (!Number.isFinite(Quantity)) return Parameters;
    if (Control.direct) return WriteMaterialParameter(Parameters, Control.key, Math.max(Control.min, Math.min(Control.max, Control.step === 1 ? Math.round(Quantity) : Quantity)));
    let Result = Parameters;
    const Position = Math.max(0, Math.min(1, Quantity));
    for (const Target of Control.targets)
    {
        const Mapped = Target.min + (Target.max - Target.min) * Position;
        Result = WriteMaterialParameter(Result, Target.key, Target.integer ? Math.round(Mapped) : Mapped);
    }
    return Result;
}
export function ExposeMaterialVariable(Parameters, VariableIdentity, Configuration)
{
    const Result = structuredClone(Parameters), Variable = Result.Authoring.Variables.find(Entry => Entry.Identity === VariableIdentity);
    if (!Variable) throw Error("Shared variable is missing.");
    Variable.Exposure = Configuration ? { ...Configuration } : null;
    Result.Authoring = NormalizeMaterialAuthoring(Result.Authoring);
    return Result;
}
export function ProjectDesignerVariable(Parameters, Variable, Quantity)
{
    if (!Variable.Exposure) throw Error("Variable is not exposed to Properties.");
    if (Variable.Type === "scalar") Quantity = Math.max(Variable.Exposure.Minimum, Math.min(Variable.Exposure.Maximum, Quantity));
    return WriteMaterialParameter(Parameters, Variable.Address, Quantity);
}
export function ResolveAuthoringParameters(Normalized, Input, ProjectionEnabled = true)
{
    if (!Input?.Authoring) return Normalized;
    const Authoring = NormalizeMaterialAuthoring(Input.Authoring), Result = { ...Normalized, Authoring };
    const Addresses = new Set([...Authoring.Generators.flatMap(Generator => Generator.Inputs), ...Authoring.Layers.flatMap(Layer => Layer.Channels.map(Channel => ChannelAddress(Layer, Channel))), ...Authoring.Variables.map(Variable => Variable.Address), ...Object.keys(Authoring.Links)]);
    for (const Address of Addresses)
    {
        if (Address.includes(":")) continue;
        const Quantity = ReadMaterialParameter(Input, Address);
        if (Quantity !== undefined) Result[Address] = Quantity;
    }
    if (!ProjectionEnabled) return Result;
    for (const Layer of Authoring.Layers)
    {
        if (Layer.Model === "surface") for (const [Channel, Specification] of Object.entries(PbrChannels))
        {
            if (!Layer.Enabled || !Layer.Channels.includes(Channel)) Result[Specification.Target] = Specification.Default ?? Specification.Minimum ?? [0, 0, 1];
        }
        if (Layer.Model === "coat")
        {
            if (!Layer.Enabled || !Layer.Channels.includes("coverage")) Result.coat = 0;
            if (!Layer.Channels.includes("roughness")) Result.coatRoughness = .5;
            if (!Layer.Channels.includes("ior")) Result.coatIor = 1.5;
        }
        if (Layer.Model === "flakes")
        {
            const CoverageSource = Authoring.Sources[ResolveAddress(Input, ChannelAddress(Layer, "coverage"))];
            Result.FlakeWeight = CoverageSource && CoverageSource.Mode !== "value" ? 1 : ReadMaterialParameter(Input, ChannelAddress(Layer, "coverage")) ?? 1;
            const Generator = Authoring.Generators.find(Entry => Entry.Identity === Layer.Generator);
            if (!Layer.Enabled || !Generator?.Enabled || !Layer.Channels.includes("coverage")) Result.flakes = 0;
            Result.FlakeOutputs = Object.fromEntries(["colour", "roughness", "metallic", "normal"].map(Channel => { const Source = Authoring.Sources[ResolveAddress(Input, ChannelAddress(Layer, Channel))]; return [Channel, Layer.Channels.includes(Channel) && (!Source || Source.Mode === "value")]; }));
        }
    }
    for (const Generator of Authoring.Generators) if (!Generator.Coded && !Generator.Enabled && !["flakes", "scratches"].includes(Generator.Procedure)) Result.type = -1;
    const Scratch = Authoring.Generators.find(Generator => Generator.Procedure === "scratches");
    if (Scratch && !Scratch.Enabled) { Result.metalScratches = false; if (Result.type === 29) Result.scratchDensity = 0; }
    return Result;
}

export function DeclareCodedField(Parameters, Procedure, Name, Category)
{
    const Result = structuredClone(Parameters), Identity = AuthoringIdentity();
    Result.Authoring.Generators.push({ Identity, Name: Name.slice(0, 80), Category: Category.slice(0, 60), Procedure, Enabled: true, Coded: true,
        Inputs: ["scale", "seed", "angle", "amount", ...(Procedure.startsWith("camouflage") ? ["colour0", "colour1", "colour2", "colour3", "softness", "pixelSize"] : [])].map(Key => `field:${Identity}:${Key}`), Outputs: ["colour", "roughness", "metallic", "normal", "coverage"] });
    if (Procedure.startsWith("camouflage")) CamouflagePalette.forEach((Colour, Index) => { Result.Authoring.Inputs[`field:${Identity}:colour${Index}`] = Colour; });
    if (Procedure.startsWith("camouflage")) { Result.Authoring.Inputs[`field:${Identity}:softness`] = .012; Result.Authoring.Inputs[`field:${Identity}:pixelSize`] = 4; }
    for (const [Key, Quantity] of Object.entries({ scale: 8, seed: 12, angle: 0, amount: 1 })) Result.Authoring.Inputs[`field:${Identity}:${Key}`] = Quantity;
    return Result;
}
export function DeclareSharedParameter(Parameters, Address, Specification, Name)
{
    const Result = structuredClone(Parameters);
    const Variable = { Identity: AuthoringIdentity(), Name, Address: ResolveAddress(Parameters, Address), Type: Specification.Type,
        ...(Specification.Type === "scalar" ? { Minimum: Specification.Minimum, Maximum: Specification.Maximum } : {}), Exposure: null };
    Result.Authoring.Variables.push(Variable);
    Result.Authoring = NormalizeMaterialAuthoring(Result.Authoring);
    return Result;
}
export function LinkMaterialParameter(Parameters, Address, Identity)
{
    const Result = structuredClone(Parameters);
    Result.Authoring.Links[Address] = Identity;
    Result.Authoring = NormalizeMaterialAuthoring(Result.Authoring);
    return Result;
}
export function UnlinkMaterialParameter(Parameters, Address)
{
    const Result = structuredClone(Parameters), Root = ResolveAddress(Result, Address), Quantity = structuredClone(ReadMaterialParameter(Result, Address));
    if (Result.Authoring.Sources[Root]) Result.Authoring.Sources[Address] = structuredClone(Result.Authoring.Sources[Root]);
    delete Result.Authoring.Links[Address];
    return WriteMaterialParameter(Result, Address, Quantity);
}

//============================================================================================================================================
//                                                             MATERIALPANEL.JSX                                                              
//============================================================================================================================================
// 🧩 PBR channel values, texture maps and inline generators with shared designer-control projections.

import React, { useEffect, useMemo, useState } from "react";
import { ChevronDown, X, Plus } from "lucide-react";
import RecipeInspector from "./RecipeInspector.jsx";
import { Slider, ColorRamp } from "./MaterialControls.jsx";
import { getRecipe } from "./materialProfiles.js";
import { GeneratorDefinitions } from "./FieldSpecification.js";
import { importTextureSource } from "./textureSource.js";
import PatternSurfaceChannelControls from "./PatternSurfaceChannelControls.jsx";
import { PbrChannels, AuthoringIdentity, ChannelAddress, DeclareMaterialAuthoring, NormalizeMaterialAuthoring, ReadMaterialParameter, WriteMaterialParameter, WritePbrChannel, DeclareCodedField, DeclareSharedParameter, LinkMaterialParameter, UnlinkMaterialParameter, ResolveAddress, ExposeMaterialVariable, ProjectDesignerVariable } from "./MaterialSpecification.js";
import { RasterChannelPreview } from "./FieldProjection.js";
import "./MaterialPanel.css";

const Caption = Text => Text.replace(/([a-z])([A-Z])/g, "$1 $2").replace(/^./, Letter => Letter.toUpperCase());
const Match = (Specification, Variable) => Specification.Type === Variable.Type && (Specification.Type !== "scalar" || (Variable.Minimum >= Specification.Minimum && Variable.Maximum <= Specification.Maximum));
function FieldPreview({ Parameters, Layer, Channel, Source })
{
    const [Preview, ConfigurePreview] = useState(null);
    useEffect(() =>
    {
        let Cancelled = false;
        ConfigurePreview(null);
        if (Source?.Mode && Source.Mode !== "value") RasterChannelPreview(Parameters, Layer.Identity, Channel).then(Image => { if (!Cancelled) ConfigurePreview(Image); }).catch(() => {});
        return () => { Cancelled = true; };
    }, [Parameters, Layer.Identity, Channel]);
    return Preview ? <img src={Preview} alt={`${PbrChannels[Channel].Caption} source output`}/> : null;
}
function ParameterControl({ Parameters, Address, Specification, OnChange, Label = Specification.Caption })
{
    const Quantity = ReadMaterialParameter(Parameters, Address) ?? Specification.Default ?? (Specification.Type === "colour" ? "#ffffff" : Specification.Type === "vector" ? [0, 0, 1] : Specification.Minimum);
    if (Specification.Type === "colour") return <label className="mw-colour">{Label}<input type="color" aria-label={Label} value={Quantity} onChange={Event => OnChange(Event.target.value)}/><small>{Quantity}</small></label>;
    if (Specification.Type === "vector") return <div className="mw-vector">{Quantity.map((Component, Index) => <label key={Index}>{"XYZ"[Index]}<input type="number" aria-label={`${Label} ${"XYZ"[Index]}`} min="-1" max="1" step=".01" value={Component} onChange={Event => { if (Event.target.value !== "" && Event.target.validity.valid) { const Components = [...Quantity]; Components[Index] = Number(Event.target.value); OnChange(Components); } }}/></label>)}</div>;
    return <Slider label={Label} value={Quantity} min={Specification.Minimum} max={Specification.Maximum} step={Specification.Step || .01} unit={Specification.Unit || ""} onChange={OnChange}/>;
}
function GeneratorInputs(Parameters, Generator)
{
    if (Generator.Coded) return Generator.Inputs.map(Address =>
    {
        const Key = Address.split(":").at(-1);
        if (/^colour\d+$/.test(Key)) return { Address, Caption: ["Sand colour", "Olive colour", "Earth colour", "Shadow colour"][Number(Key.slice(6))] || `Colour ${Number(Key.slice(6)) + 1}`, Type: "colour", Default: "#ffffff" };
        const Limits = { scale: [1, 200, 1], seed: [0, 9999, 1], angle: [-180, 180, 1], amount: [0, 1, .01], softness: [.001, .1, .001], pixelSize: [1, 16, 1] }[Key];
        if (Key === "softness" && Generator.Procedure === "camouflage" || Key === "pixelSize" && Generator.Procedure === "camouflage-smooth") return null;
        return { Address, Caption: Key === "amount" && Generator.Procedure.startsWith("camouflage") ? "Patch contrast" : Caption(Key), Type: "scalar", Minimum: Limits[0], Maximum: Limits[1], Step: Limits[2] };
    }).filter(Boolean);
    const Controls = getRecipe(Parameters).controls;
    return Generator.Inputs.map(Address =>
    {
        const Direct = Controls.find(Control => Control.direct && Control.key === Address), Target = Controls.flatMap(Control => Control.targets || []).find(Entry => Entry.key === Address);
        return { Address, Caption: Direct?.label || Caption(Address), Type: "scalar", Minimum: Direct?.min ?? Math.min(Target?.min ?? 0, Target?.max ?? 1), Maximum: Direct?.max ?? Math.max(Target?.min ?? 0, Target?.max ?? 1), Step: Direct?.step || (Target?.integer ? 1 : .001), Unit: Direct?.unit || "" };
    });
}
export default function MaterialPanel({ Parameters, ConfigureParameters, Mode, Notify, OpenWorkspace })
{
    const Document = useMemo(() => Parameters.Authoring || DeclareMaterialAuthoring(Parameters), [Parameters.Authoring]);
    const [OpenChannels, ConfigureOpenChannels] = useState({}), [Editing, ConfigureEditing] = useState(null), [LibraryOpen, ConfigureLibraryOpen] = useState(true), [FieldName, ConfigureFieldName] = useState("Fractal breakup"), [Category, ConfigureCategory] = useState("Surface fields"), [Renaming, ConfigureRenaming] = useState(null), [NewName, ConfigureNewName] = useState("");
    useEffect(() => { if (!Parameters.Authoring) ConfigureParameters(Current => ({ ...Current, Authoring: Document })); }, [Parameters.Authoring, Document]);
    useEffect(() => {
        if (Parameters.metalScratches && Parameters.Authoring && !Document.Generators.some(Generator => Generator.Procedure === "scratches")) ConfigureParameters(Current => ({ ...Current, Authoring: { ...Current.Authoring, Generators: [...Current.Authoring.Generators, DeclareMaterialAuthoring(Current).Generators.find(Generator => Generator.Procedure === "scratches")] } }));
    }, [Parameters.metalScratches, Document]);
    const Change = Projection => ConfigureParameters(Current =>
    {
        try { const Next = Projection(structuredClone(Current)); if (Next.Authoring) Next.Authoring = NormalizeMaterialAuthoring(Next.Authoring); return Next; }
        catch (Error) { queueMicrotask(() => Notify(Error.message)); return Current; }
    });
    const Edit = Mutation => Change(Current => { Mutation(Current.Authoring); return Current; });
    function Share(Address, Specification, Name)
    {
        const Root = ResolveAddress(Parameters, Address), Existing = Document.Variables.find(Variable => Variable.Address === Root);
        if (Existing) { ConfigureEditing(Existing.Identity); ConfigureLibraryOpen(true); return; }
        Name = Name.slice(0, 54);
        let Unique = Name, Ordinal = 2;
        while (Document.Variables.some(Variable => Variable.Name === Unique)) Unique = `${Name} ${Ordinal++}`;
        const Seeded = ReadMaterialParameter(Parameters, Address) === undefined ? WriteMaterialParameter(Parameters, Address, Specification.Default ?? (Specification.Type === "vector" ? [0, 0, 1] : Specification.Type === "colour" ? "#ffffff" : Specification.Minimum)) : Parameters;
        const Next = DeclareSharedParameter(Seeded, Address, Specification, Unique);
        ConfigureParameters(Next); ConfigureEditing(Next.Authoring.Variables.at(-1).Identity); ConfigureLibraryOpen(true);
    }
    const ToggleExposure = Variable => Change(Current => ExposeMaterialVariable(Current, Variable.Identity, Variable.Exposure ? null : { Name: Variable.Name, ...(Variable.Type === "scalar" ? { Minimum: Variable.Minimum, Maximum: Variable.Maximum } : {}) }));
    function VariableActions(Address, Specification, Name)
    {
        const Variable = Document.Variables.find(Entry => Entry.Address === ResolveAddress(Parameters, Address));
        return <div className="mw-exposure"><button onClick={() => Share(Address, Specification, Name)}>◇ {Variable ? "Edit variable" : "Make variable"}</button>{Variable && <label><input type="checkbox" checked={!!Variable.Exposure} onChange={() => ToggleExposure(Variable)}/>Expose to Properties</label>}</div>;
    }
    function GeneratorSheet(Generator, Expanded = false)
    {
        if (!Generator) return null;
        const Index = Document.Generators.findIndex(Entry => Entry.Identity === Generator.Identity);
        const Controls = GeneratorInputs(Parameters, Generator), Palette = Controls.filter(Control => Control.Type === "colour");
        return <details className="mw-generator" key={Generator.Identity} open={Expanded} aria-label={`${Generator.Name} generator sheet`}><summary>{Generator.Name}<small>{Generator.Category}</small></summary><label>Name<input aria-label="Generator instance name" maxLength={80} value={Generator.Name} onChange={Event => Edit(Next => { Next.Generators[Index].Name = Event.target.value; })}/></label><label>Category<input aria-label="Generator category" maxLength={60} value={Generator.Category} onChange={Event => Edit(Next => { Next.Generators[Index].Category = Event.target.value; })}/></label><label className="mw-toggle"><input type="checkbox" aria-label={`Enable ${Generator.Name} generator`} checked={Generator.Enabled} onChange={Event => Edit(Next => { Next.Generators[Index].Enabled = Event.target.checked; })}/>Enabled</label><p className="mw-help">{Generator.Outputs.join(" · ")} outputs</p>
                    {Generator.Procedure.startsWith("camouflage") && <><label>Edge style<select aria-label="Camouflage edge style" value={Generator.Procedure} onChange={Event => Edit(Next => {
                        const Entry = Next.Generators[Index]; Entry.Procedure = Event.target.value;
                        for (const [Key, Quantity] of Object.entries({ softness: .012, pixelSize: 4 })) { const Address = `field:${Entry.Identity}:${Key}`; if (!Entry.Inputs.includes(Address)) Entry.Inputs.push(Address); if (Next.Inputs[Address] === undefined) Next.Inputs[Address] = Quantity; }
                    })}><option value="camouflage">Pixelated</option><option value="camouflage-smooth">Smooth / organic</option></select></label>
                    <fieldset className="mw-camouflage-palette"><legend>Colour array / ramp</legend><div className="mw-palette-preview" style={{ background: `linear-gradient(90deg,${Palette.map(Control => ReadMaterialParameter(Parameters, Control.Address)).join(",")})` }}/>
                        {Palette.map(Control => <div key={Control.Address}><ParameterControl Parameters={Parameters} Address={Control.Address} Specification={Control} OnChange={Quantity => Change(Current => WriteMaterialParameter(Current, Control.Address, Quantity))}/>{VariableActions(Control.Address, Control, `${Generator.Name} ${Control.Caption}`)}</div>)}
                        <div className="mw-palette-actions"><button disabled={Palette.length >= 8} onClick={() => Edit(Next => { const Address = `field:${Generator.Identity}:colour${Math.max(...Palette.map(Control => Number(Control.Address.split(":").at(-1).slice(6)))) + 1}`; Next.Generators[Index].Inputs.push(Address); Next.Inputs[Address] = "#827957"; })}>+ Add colour</button><button disabled={Palette.length <= 2} onClick={() => Edit(Next => { const Address = Palette.at(-1).Address; Next.Generators[Index].Inputs = Next.Generators[Index].Inputs.filter(Input => Input !== Address); })}>Remove last colour</button></div>
                    </fieldset></>}
                    {Controls.filter(Control => Control.Type !== "colour" || !Generator.Procedure.startsWith("camouflage")).map(Specification => <div className="mw-generator-input" key={Specification.Address}><ParameterControl Parameters={Parameters} Address={Specification.Address} Specification={Specification} OnChange={Quantity => Change(Current => WriteMaterialParameter(Current, Specification.Address, Quantity))}/><label className="mw-param-reference">Shared input<select aria-label={`${Generator.Name} ${Specification.Caption} variable`} value={Document.Links[Specification.Address] || ""} onChange={Event => Change(Current => Event.target.value ? LinkMaterialParameter(Current, Specification.Address, Event.target.value) : UnlinkMaterialParameter(Current, Specification.Address))}><option value="">Independent</option>{Document.Variables.filter(Variable => Match(Specification, Variable) && Variable.Address !== Specification.Address).map(Variable => <option key={Variable.Identity} value={Variable.Identity}>{Variable.Name}</option>)}</select></label>{VariableActions(Specification.Address, Specification, `${Generator.Name} ${Specification.Caption}`)}</div>)}
                    {Generator.Procedure === "flakes" && <ColorRamp params={Parameters} update={(Key, Quantity) => Change(Current => WriteMaterialParameter(Current, Key, Quantity))} setParams={ConfigureParameters}/>}<small>Shared instance: edits here affect every channel using it. Expose inputs to Properties with ◇.</small>
                </details>;
    }
    function SourceControls(Layer, Channel, Expanded = true)
    {
        const Specification = PbrChannels[Channel], Address = ChannelAddress(Layer, Channel), Root = ResolveAddress(Parameters, Address), Source = Document.Sources[Root] || { Mode: "value", Stops: ["#000000", "#ffffff"] };
        const NativeGenerators = Layer.Model === "flakes" && Channel !== "coverage" ? Document.Generators.filter(Generator => Generator.Identity === Layer.Generator) : Layer.Model === "surface" ? Document.Generators.filter(Generator => !Generator.Coded && Generator.Procedure !== "flakes" && Generator.Outputs.includes(Channel)) : [];
        const NativeField = NativeGenerators.length > 0;
        const AmendSource = Patch => Edit(Next => { Next.Sources[Root] = { ...Source, ...Patch }; });
        return <div className="mw-source-controls">
            <label>Input type<select aria-label={`${Layer.Name} ${Specification.Caption} source`} value={Source.Mode} onChange={Event => { AmendSource({ Mode: Event.target.value, Direct: true }); ConfigureOpenChannels(Current => ({ ...Current, [`${Layer.Identity}:${Channel}`]: true })); }}><option value="value">{NativeField ? "Generator · native material" : Specification.Type === "colour" ? "Value · solid colour" : "Value · single value"}</option>{NativeField && <option value="solid">Value · single value</option>}<option value="texture">Texture / image</option><optgroup label="Generators"><option value="linear">Linear gradient</option><option value="radial">Radial gradient</option><option value="conic">Conic gradient</option><option value="ramp">Colour ramp</option><option value="field">Procedural generator…</option>{Parameters.pattern && <option value="pattern">Applied pattern</option>}</optgroup></select></label>
            {Expanded && <>
            {["value", "solid"].includes(Source.Mode) && !(NativeField && Source.Mode === "value") && <ParameterControl Parameters={Parameters} Address={Address} Specification={Specification} OnChange={Quantity => Change(Current => WritePbrChannel(Current, Layer, Channel, Quantity))}/>}
            {NativeField && Source.Mode === "value" && <section className="mw-inline-generator" aria-label={`${Layer.Name} ${Specification.Caption} native generator controls`}>{NativeGenerators.map(Generator => GeneratorSheet(Generator, true))}</section>}
            {!["value", "solid"].includes(Source.Mode) && Source.Direct !== true && <details className="mw-source-adjustment"><summary>Existing source adjustment</summary><ParameterControl Parameters={Parameters} Address={Address} Specification={Specification} Label={`${Specification.Caption} ${Specification.Type === "scalar" ? "map maximum" : Specification.Type === "colour" ? "tint" : "offset"}`} OnChange={Quantity => Change(Current => WritePbrChannel(Current, Layer, Channel, Quantity))}/><small>Retained for previously authored maps. Reselect the input type to use its output directly.</small></details>}
            {["linear", "radial", "conic"].includes(Source.Mode) && <div className="mw-gradient"><input type="color" aria-label="Gradient start" value={Source.Stops?.[0] || "#000000"} onChange={Event => AmendSource({ Stops: [Event.target.value, Source.Stops?.[1] || "#ffffff"] })}/><input type="color" aria-label="Gradient end" value={Source.Stops?.[1] || "#ffffff"} onChange={Event => AmendSource({ Stops: [Source.Stops?.[0] || "#000000", Event.target.value] })}/><div style={{ background: `linear-gradient(90deg,${(Source.Stops || ["#000000", "#ffffff"]).join(",")})` }}/></div>}
            {Source.Mode === "ramp" && <div className="mw-ramp-stops">{(Source.Stops || ["#000000", "#ffffff"]).map((Colour, Index) => <label key={Index}>Stop {Index + 1}<input type="color" value={Colour} onChange={Event => AmendSource({ Stops: (Source.Stops || ["#000000", "#ffffff"]).map((Entry, Ordinal) => Ordinal === Index ? Event.target.value : Entry) })}/></label>)}<button disabled={Source.Stops?.length >= 8} onClick={() => AmendSource({ Stops: [...(Source.Stops || ["#000000", "#ffffff"]), "#808080"] })}>+ Ramp stop</button></div>}
            {Source.Mode === "texture" && <section className="mw-texture-source" aria-label={`${Layer.Name} ${Specification.Caption} texture input`}>
                <div className="mw-texture-preview" aria-label="Square texture preview">{Source.Texture ? <img src={Source.Texture.dataUrl} alt="Embedded channel source"/> : <span>No texture selected</span>}</div>
                <label>Load from file<input aria-label={`${Layer.Name} ${Specification.Caption} texture`} type="file" accept="image/png,image/jpeg,image/webp" onChange={async Event => { const File = Event.target.files?.[0]; if (!File) return; try { const Texture = await importTextureSource(File); AmendSource({ Texture }); } catch (Error) { Notify(Error.message); } }}/></label>
                {Source.Texture && <small>{Source.Texture.name} · {Source.Texture.width} × {Source.Texture.height}</small>}
            </section>}
            {Source.Mode === "field" && <label>Shared generator<select aria-label={`${Layer.Name} ${Specification.Caption} generator`} value={Source.Generator || ""} onChange={Event => AmendSource({ Generator: Event.target.value })}><option value="">Choose a field instance…</option>{Document.Generators.filter(Generator => Generator.Coded).map(Generator => <option key={Generator.Identity} value={Generator.Identity}>{Generator.Name} · {Generator.Category}</option>)}</select><small>Reuse a named instance, or create one below. Its inputs are edited here in the channel.</small></label>}
            {Source.Mode === "field" && <section className="mw-inline-generator" aria-label={`${Layer.Name} ${Specification.Caption} generator controls`}>
                <details className="mw-new-field" open={!Source.Generator}><summary>Create generator in this channel</summary>
                    <label>Instance name<input aria-label="New generator name" maxLength={80} value={FieldName} onChange={Event => ConfigureFieldName(Event.target.value)}/></label>
                    <label>Category<input aria-label="New generator category" maxLength={60} value={Category} onChange={Event => ConfigureCategory(Event.target.value)}/></label>
                    <label>Procedure<select aria-label="Add coded generator" value="" onChange={Event => { const Procedure = Event.target.value; if (Procedure) Change(Current => { const Next = DeclareCodedField(Current, Procedure, FieldName.trim() || "Surface field", Category.trim() || "Surface fields"); Next.Authoring.Sources[ResolveAddress(Next, Address)] = { ...Source, Mode: "field", Generator: Next.Authoring.Generators.at(-1).Identity }; return Next; }); }}><option value="">Choose procedure…</option>{GeneratorDefinitions.map(Generator => <option key={Generator.identity} value={Generator.identity}>{Generator.label}</option>)}</select></label>
                </details>
                {GeneratorSheet(Document.Generators.find(Generator => Generator.Identity === Source.Generator), true)}
            </section>}
            {Source.Mode === "pattern" && <section className="mw-inline-generator"><p className="mw-help">{Specification.Caption} output from the applied pattern.</p><button onClick={() => OpenWorkspace("pattern")}>Edit pattern artwork</button><PatternSurfaceChannelControls Document={Parameters.pattern} Selection={Parameters.pattern.layers.map((Entry, Ordinal) => Ordinal)} OnFinalize={Pattern => ConfigureParameters(Current => ({ ...Current, pattern: Pattern }))} OnError={Notify}/></section>}
            {(!NativeField || Source.Mode !== "value") && <><label>Shared parameter<select aria-label={`${Layer.Name} ${Specification.Caption} shared variable`} value={Document.Links[Address] || ""} onChange={Event => Change(Current => Event.target.value ? LinkMaterialParameter(Current, Address, Event.target.value) : UnlinkMaterialParameter(Current, Address))}><option value="">Independent</option>{Document.Variables.filter(Variable => Match(Specification, Variable) && Variable.Address !== Address).map(Variable => <option key={Variable.Identity} value={Variable.Identity}>{Variable.Name}</option>)}</select></label>{VariableActions(Address, Specification, `${Layer.Name} ${Specification.Caption}`)}</>}
            </>}
        </div>;
    }
    const Exposed = Document.Variables.filter(Variable => Variable.Exposure);
    return <div className="mw-panel" aria-label="Material authoring workspace">
        <div className="mw-scroll">
            {Mode === "Properties" && <><div className="mw-intro"><span>DESIGNER VIEW</span><h3>Properties</h3><p>Renamed, range-limited projections into layer channels and generator inputs. These are not another set of material values.</p></div><RecipeInspector params={Parameters} update={(Address, Quantity) => Change(Current => WriteMaterialParameter(Current, Address, Quantity))} setParams={ConfigureParameters}/><section className="mw-exposed-properties"><h4>Exposed variables</h4>{!Exposed.length && <p className="mw-help">Enable “Expose to Properties” on a variable in the bottom list.</p>}{Exposed.map(Variable => <section className="mw-property" key={Variable.Identity}><ParameterControl Parameters={Parameters} Address={Variable.Address} Specification={{ ...Variable, Caption: Variable.Exposure.Name, ...(Variable.Type === "scalar" ? { Minimum: Variable.Exposure.Minimum, Maximum: Variable.Exposure.Maximum } : {}) }} OnChange={Quantity => Change(Current => ProjectDesignerVariable(Current, Variable, Quantity))}/><small>Linked to {Variable.Name}</small></section>)}</section></>}
            {Mode !== "Properties" && <><div className="mw-intro"><span>PHYSICAL VIEW</span><h3>PBR layer stack</h3><p>Outside → inside. Every field accepts a value, texture or generator. Its selected input and editable controls live directly under that field.</p></div>{Document.Layers.map((Layer, Index) => <section className="mw-layer" key={Layer.Identity} aria-label={`${Layer.Name} PBR layer`}><header><span className="mw-ordinal">{String(Index + 1).padStart(2, "0")}</span><input aria-label="PBR layer name" maxLength={80} value={Layer.Name} onChange={Event => Edit(Next => { Next.Layers[Index].Name = Event.target.value; })}/><input type="checkbox" aria-label={`Enable ${Layer.Name}`} checked={Layer.Enabled} onChange={Event => Edit(Next => { Next.Layers[Index].Enabled = Event.target.checked; })}/>{Layer.Model === "detail" && <button className="icon-button" aria-label={`Remove ${Layer.Name} layer`} onClick={() => Edit(Next => { Next.Layers.splice(Index, 1); })}><X size={12}/></button>}</header><small>{Layer.Model === "coat" ? "Dielectric coat" : Layer.Model === "flakes" ? "PBR contribution from particle field" : Layer.Model === "surface" ? "Surface scattering body" : "PBR channel overlay"}</small>
                <details className="mw-channel-menu"><summary><Plus size={12}/> Add / remove channels <small>{Layer.Channels.length}</small></summary><div>{Object.entries(PbrChannels).filter(([Channel]) => Layer.Model !== "coat" || ["coverage", "roughness", "ior", "normal"].includes(Channel)).map(([Channel, Specification]) => <label key={Channel}><input type="checkbox" aria-label={`${Layer.Name} ${Specification.Caption} channel`} checked={Layer.Channels.includes(Channel)} onChange={Event => Edit(Next => { Next.Layers[Index].Channels = Event.target.checked ? [...Layer.Channels, Channel] : Layer.Channels.filter(Entry => Entry !== Channel); })}/>{Specification.Caption}</label>)}</div></details>
                {Layer.Channels.map(Channel =>
                {
                    const Address = ChannelAddress(Layer, Channel), Specification = PbrChannels[Channel], Quantity = ReadMaterialParameter(Parameters, Address) ?? Specification.Default, Open = OpenChannels[`${Layer.Identity}:${Channel}`] ?? true, Source = Document.Sources[ResolveAddress(Parameters, Address)];
                    return <section className="mw-channel" key={Channel} aria-label={`${Layer.Name} ${Specification.Caption} card`}><header><button className="mw-disclosure" aria-expanded={!!Open} onClick={() => ConfigureOpenChannels(Current => ({ ...Current, [`${Layer.Identity}:${Channel}`]: !Open }))}><ChevronDown size={12}/>{Specification.Caption}</button>{(!Address.startsWith("flake-output:") || Channel === "coverage") && <button title="Make or edit shared variable" className="icon-button" aria-label={`Share ${Layer.Name} ${Specification.Caption}`} onClick={() => Share(Address, Specification, `${Layer.Name} ${Specification.Caption}`)}>◇</button>}<button className="icon-button" aria-label={`Remove ${Layer.Name} ${Specification.Caption}`} onClick={() => Edit(Next => { Next.Layers[Index].Channels = Layer.Channels.filter(Entry => Entry !== Channel); })}><X size={11}/></button></header>
                        {Source && ["field", "linear", "radial", "conic", "ramp", "pattern"].includes(Source.Mode) && <div className="mw-card-output"><FieldPreview Parameters={Parameters} Layer={Layer} Channel={Channel} Source={Source}/><span>{Source.Mode === "field" ? Document.Generators.find(Generator => Generator.Identity === Source.Generator)?.Name || "Choose generator" : Caption(Source.Mode)}</span></div>}
                        {SourceControls(Layer, Channel, Open)}
                    </section>;
                })}

            </section>)}<button className="mw-add-layer" onClick={() => Edit(Next => { if (Next.Layers.length >= 16) throw Error("Maximum 16 layers."); const Position = Next.Layers[0]?.Model === "coat" ? 1 : 0; Next.Layers.splice(Position, 0, { Identity: AuthoringIdentity(), Name: "PBR detail layer", Model: "detail", Enabled: true, Channels: ["coverage"] }); })}>+ Add PBR detail layer</button></>}

        </div>
        <section className="mw-variable-dock" aria-label="Material variables"><header><h4>Variables <small>{Document.Variables.length}</small></h4><button className="icon-button" aria-label="Toggle variable list" aria-expanded={LibraryOpen} onClick={() => ConfigureLibraryOpen(!LibraryOpen)}>{LibraryOpen ? "−" : "+"}</button></header>{LibraryOpen && <div className="mw-variable-list">{!Document.Variables.length && <p className="mw-help">Use ◇ on a channel or generator input. Expose selected variables to Properties.</p>}{Document.Variables.map(Variable => <section className="mw-variable" key={Variable.Identity}><header><strong>{Variable.Name}</strong><button onClick={() => ConfigureEditing(Editing === Variable.Identity ? null : Variable.Identity)}>{Editing === Variable.Identity ? "Close" : "Edit"}</button><button onClick={() => { ConfigureRenaming(Variable.Identity); ConfigureNewName(Variable.Name); }}>Rename</button></header><small>{Variable.Address} · {Object.values(Document.Links).filter(Reference => Reference === Variable.Identity).length + 1} uses</small>{Editing === Variable.Identity && <>
            <ParameterControl Parameters={Parameters} Address={Variable.Address} Specification={{ ...Variable, Caption: Variable.Name }} OnChange={Quantity => Change(Current => WriteMaterialParameter(Current, Variable.Address, Quantity))}/><label className="mw-toggle"><input type="checkbox" aria-label={`Expose ${Variable.Name} to Properties`} checked={!!Variable.Exposure} onChange={() => ToggleExposure(Variable)}/>Expose to Properties</label>
            {Variable.Exposure && <fieldset className="mw-designer-projection"><legend>Designer projection</legend><label>Property label<input aria-label="Exposed property label" maxLength={80} value={Variable.Exposure.Name} onChange={Event => Edit(Next => { Next.Variables.find(Entry => Entry.Identity === Variable.Identity).Exposure.Name = Event.target.value; })}/></label>{Variable.Type === "scalar" && <div className="mw-range">{["Minimum", "Maximum"].map(Key => <label key={Key}>{Key}<input type="number" aria-label={`Exposed property ${Key.toLowerCase()}`} min={Variable.Minimum} max={Variable.Maximum} step="any" value={Variable.Exposure[Key]} onChange={Event => { if (Event.target.value !== "" && Number.isFinite(Number(Event.target.value))) Edit(Next => { Next.Variables.find(Entry => Entry.Identity === Variable.Identity).Exposure[Key] = Number(Event.target.value); }); }}/></label>)}</div>}<small>Only the Properties control is restricted. The PBR/source editor retains the full physical range.</small></fieldset>}
        </>}</section>)}</div>}</section>
        {Renaming && <div className="mw-rename" role="dialog" aria-modal="true" aria-label="Rename material variable"><form onSubmit={Event => { Event.preventDefault(); const Name = NewName.trim(); if (!Name || Document.Variables.some(Variable => Variable.Identity !== Renaming && Variable.Name === Name)) return Notify("Choose a unique variable name."); Edit(Next => { Next.Variables.find(Variable => Variable.Identity === Renaming).Name = Name; }); ConfigureRenaming(null); }}><h3>Rename variable</h3><input autoFocus required maxLength={60} aria-label="Variable name" value={NewName} onChange={Event => ConfigureNewName(Event.target.value)}/><div><button type="button" onClick={() => ConfigureRenaming(null)}>Cancel</button><button type="submit">Save name</button></div></form></div>}
    </div>;
}

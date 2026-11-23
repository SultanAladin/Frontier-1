// PARTICLEEDITOR.JS
// 🧩 Particle editor (standalone web app): the ten Filament looks as stackable particle layers inside the Frontier Editor layout —
//    outliner (layers, scene, post, recipe) · WebGL viewport with its bar and readout · inspector of cards · status bar.
// 📝 The renderer is Particles/FilamentRig.js (the CabinHmi Filament showroom also runs on it). The UI is the Frontier
//    Editor's stylesheet and inspector builders (Style/FrontierEditor.css, Interface/), copied unchanged from the Proving
//    Grounds exhibit so this folder serves on its own. Every edit applies live and is kept in
//    the browser (localStorage); recipes export and import as JSON.
//    URL options: ?fresh=1 (ignore the saved recipe) · ?recipe=Launch (start from a built-in recipe) · ?solo=Flora · ?select=Camera
//    · ?time=6 · ?capture=1 (headless captures: no animation loop, one frame per Seek).
//    Keys: 1–9, 0 select a layer · E show/hide it · Space pause · R restart time · O auto orbit · P columns · V clean view · H readout.

import { RaiseRig, Looks, Palettes, CarModes, MaxCount } from './Particles/FilamentRig.js';
import { EditorControls, ClearElement, AssignStyle } from './Interface/EditorControls.js';
import { ConstructGlyph } from './Interface/EditorIcons.js';

const Query = new URLSearchParams(location.search);
const Capture = Query.get('capture') === '1';
const StorageKey = 'ParticleEditor.Recipe';
const RefreshInterval = 200;                                          // [ms] readouts, outliner and status bar at 5 Hz
const GroupOrder = ['Layers', 'Scene', 'Post', 'Recipe'];
const LookGlyphs = { Streaks: 'TrendingUp', Assemble: 'Boxes', Airflow: 'Wind', Turbulence: 'Flame', Clones: 'LayoutGrid', Charge: 'Zap',
                     Orbits: 'Orbit', Flora: 'Sparkles', Transition: 'Scan', FibreWave: 'Waves' };
const ToneNames = ['Base', 'Accent', 'Hot core', 'Contrast'];

function Construct(Tag, ClassName = '', Text = '')
{
    const Made = document.createElement(Tag);
    if (ClassName) Made.className = ClassName;
    if (Text) Made.textContent = Text;
    return Made;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    BRING-UP
//------------------------------------------------------------------------------------------------------------------------

const BringUp = document.getElementById('bring-up');
const BringUpBar = BringUp.querySelector('.bring-up-bar > div');
BringUpBar.style.width = '35%';
const Canvas = document.getElementById('viewport');
let Rig;
try
{
    Rig = await RaiseRig(Canvas, { Capture });
}
catch (Failure)
{
    const Panel = document.getElementById('failure');
    Panel.textContent = `Filament could not start: ${Failure?.message ?? Failure}`;
    Panel.classList.remove('hidden');
    throw Failure;
}
BringUpBar.style.width = '100%';

//------------------------------------------------------------------------------------------------------------------------
//                                                    RECIPES
//------------------------------------------------------------------------------------------------------------------------

// A recipe is the whole editable scene: which layers run and how, the set settings and the camera.
function WriteRecipe()
{
    const S = Rig.Settings;
    return {
        Format: 'FilamentRecipe', Version: 1,
        Layers: Rig.Layers.map((Layer) => ({ Look: Layer.Look.Key, Enabled: Layer.Enabled, Params: { ...Layer.Params, Tones: [...Layer.Params.Tones] } })),
        Settings: { Glow: S.Glow, Cut: S.Cut, Exposure: S.Exposure, Car: S.Car, Backdrop: S.Backdrop ? [...S.Backdrop] : null, Orbit: S.Orbit,
                    Lead: Rig.Layers[S.Lead].Look.Key, PaintFrom: S.PaintFrom, PaintTo: S.PaintTo },
        Camera: { Yaw: Rig.Goal.Yaw, Pitch: Rig.Goal.Pitch, Distance: Rig.Goal.Distance, Target: Rig.Goal.Target.toArray() },
    };
}

function ReadRecipe(Recipe, Snap = true)
{
    if (!Recipe || Recipe.Format !== 'FilamentRecipe') throw new Error('Not a Filament recipe');
    for (const Layer of Rig.Layers)
    {
        const Saved = Recipe.Layers?.find((Each) => Each.Look === Layer.Look.Key);
        Rig.ApplyDefaults(Layer);
        Layer.Enabled = !!Saved?.Enabled;
        if (Saved?.Params)
        {
            for (const Key of Object.keys(Layer.Params))
                if (Key !== 'Tones' && typeof Saved.Params[Key] === typeof Layer.Params[Key]) Layer.Params[Key] = Saved.Params[Key];
            if (Array.isArray(Saved.Params.Tones) && Saved.Params.Tones.length === 4) Layer.Params.Tones = Saved.Params.Tones.map(String);
        }
    }
    const S = Recipe.Settings ?? {};
    const Lead = Math.max(0, Looks.findIndex((Look) => Look.Key === S.Lead));
    Object.assign(Rig.Settings, {
        Glow: S.Glow ?? 1, Cut: S.Cut ?? 1.2, Exposure: S.Exposure ?? 1, Car: CarModes.includes(S.Car) ? S.Car : 'Auto',
        Backdrop: Array.isArray(S.Backdrop) ? [...S.Backdrop] : null, Orbit: !!S.Orbit, Lead,
        PaintFrom: S.PaintFrom ?? '#3a4049', PaintTo: S.PaintTo ?? '#b8d032',
    });
    if (Recipe.Camera) Rig.AimAt(Recipe.Camera, Snap);
    else Rig.AimAt(Looks[Lead].View, Snap);
}

// Built-in starting points: one look alone, or stacks that read well together.
const Stock = {
    Launch:    { Name: 'Launch reveal',  Layers: ['Transition', 'Streaks'], Lead: 'Transition', Tune: { Streaks: { Count: 30000, Gain: 0.6 } } },
    Aero:      { Name: 'Aero study',     Layers: ['Airflow', 'Assemble'],   Lead: 'Airflow',    Tune: { Assemble: { Count: 60000, Gain: 0.5 } } },
    Charging:  { Name: 'Charging bay',   Layers: ['Charge', 'Clones'],      Lead: 'Charge',     Tune: { Clones: { Count: 20000, Gain: 0.5, Variant: 1 } } },
    Reference: { Name: 'Orbit garden',   Layers: ['Orbits', 'Flora'],       Lead: 'Orbits',     Tune: { Flora: { Count: 60000, Gain: 0.45 } } },
};
function LoadStock(Key)
{
    const Recipe = Stock[Key];
    for (const Layer of Rig.Layers)
    {
        Rig.ApplyDefaults(Layer);
        Layer.Enabled = Recipe.Layers.includes(Layer.Look.Key);
        Object.assign(Layer.Params, Recipe.Tune[Layer.Look.Key] ?? {});
    }
    const Lead = Looks.findIndex((Look) => Look.Key === Recipe.Lead);
    Object.assign(Rig.Settings, { Lead, Car: 'Auto', Backdrop: null, Glow: 1, Cut: 1.2, Exposure: 1 });
    Rig.AimAt(Looks[Lead].View, false);
}
function Solo(Slot)
{
    for (const Layer of Rig.Layers) Layer.Enabled = Layer.Slot === Slot;
    Rig.Settings.Lead = Slot;
    Rig.AimAt(Looks[Slot].View, false);
}

let SaveTimer = 0;
function Persist()
{
    if (Capture) return;
    clearTimeout(SaveTimer);
    SaveTimer = setTimeout(() => { try { localStorage.setItem(StorageKey, JSON.stringify(WriteRecipe())); } catch (Failure) { } }, 300);
}

function Download(Name, Text)
{
    const Link = Construct('a');
    Link.href = URL.createObjectURL(new Blob([Text], { type: 'application/json' }));
    Link.download = Name;
    document.body.appendChild(Link);
    Link.click();
    setTimeout(() => { URL.revokeObjectURL(Link.href); Link.remove(); }, 1000);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 OUTLINE ENTRIES
//------------------------------------------------------------------------------------------------------------------------

// 📝 { Id, Group, Name, Type, Glyph, Colour(), Meta(), Mark() ("live" | "warm" | ""), Toggle?, Description, Build(Controls, Cards) }
const Format = (Count) => (Count >= 1000 ? `${Math.round(Count / 1000)}k` : String(Count));
const Entries = [
    ...Rig.Layers.map((Layer) => ({
        Id: `Layer:${Layer.Look.Key}`, Group: 'Layers', Name: Layer.Look.Name, Type: 'Particle layer', Glyph: LookGlyphs[Layer.Look.Key] ?? 'Sparkles',
        Colour: () => Layer.Params.Tones[1], Layer,
        Meta: () => `${(Layer.Slot + 1) % 10} · ${Format(Layer.Params.Count)}${Rig.Settings.Lead === Layer.Slot ? ' · lead' : ''}`,
        Mark: () => (Layer.Enabled ? (Rig.Settings.Lead === Layer.Slot ? 'live' : 'warm') : ''),
        Toggle: { Read: () => Layer.Enabled, Write: (On) => { Layer.Enabled = On; } },
        Description: Layer.Look.Tagline, Build: (C, Cards) => BuildLayer(Layer, C, Cards),
    })),
    { Id: 'Camera', Group: 'Scene', Name: 'Camera', Type: 'Orbit camera', Glyph: 'Camera', Colour: () => '#8fb8ff',
      Meta: () => `${Rig.Orbit.Distance.toFixed(1)} m`, Mark: () => (Rig.Settings.Orbit ? 'live' : ''),
      Description: 'Orbit, framing and auto turn', Build: BuildCamera },
    { Id: 'Car', Group: 'Scene', Name: 'ControlVehicle', Type: 'Vehicle body', Glyph: 'Car', Colour: () => '#c5c5c5',
      Meta: () => (Rig.Settings.Car === 'Auto' ? `auto · ${Rig.ShownCar.toLowerCase()}` : Rig.Settings.Car.toLowerCase()), Mark: () => (Rig.ShownCar !== 'Hidden' ? 'live' : ''),
      Description: 'How the body shows under the particles', Build: BuildCar },
    { Id: 'Backdrop', Group: 'Scene', Name: 'Backdrop', Type: 'Gradient dome', Glyph: 'Moon', Colour: () => (Rig.Settings.Backdrop ?? Rig.Lead.Look.Backdrop ?? ['#2a2a2a'])[0],
      Meta: () => (Rig.Settings.Backdrop ? 'custom' : 'follows lead'), Mark: () => 'live',
      Description: 'Sky colour behind everything', Build: BuildBackdrop },
    { Id: 'Post', Group: 'Post', Name: 'Bloom & tone', Type: 'Post process', Glyph: 'Sun', Colour: () => '#e0c27a',
      Meta: () => `glow ${Rig.Settings.Glow.toFixed(2)}`, Mark: () => 'live',
      Description: 'Bloom chain, ACES tone map, grain', Build: BuildPost },
    { Id: 'Time', Group: 'Post', Name: 'Time', Type: 'Clock', Glyph: 'Timer', Colour: () => '#9fe6a0',
      Meta: () => `${Rig.Clock.toFixed(1)} s`, Mark: () => (Rig.Settings.Paused ? 'warm' : 'live'),
      Description: 'Play, pause and jump in time', Build: BuildTime },
    { Id: 'Recipe', Group: 'Recipe', Name: 'Recipe', Type: 'Saved scene', Glyph: 'Layers', Colour: () => '#b4b4b4',
      Meta: () => `${Rig.Layers.filter((Layer) => Layer.Enabled).length} on`, Mark: () => '',
      Description: 'Built-in stacks, export, import', Build: BuildRecipe },
];

//------------------------------------------------------------------------------------------------------------------------
//                                                 INSPECTOR PAGES
//------------------------------------------------------------------------------------------------------------------------

function BuildLayer(Layer, C, Cards)
{
    const P = Layer.Params, Look = Layer.Look;

    const Emission = C.Card(Cards, 'Emission', 'Sparkles', { Index: 1, Subtitle: 'Particles drawn by this layer' });
    C.Metric(Emission, () => (Layer.Mesh.visible ? Layer.Geometry.instanceCount : 0), 'particles', 0,
             () => (Layer.Enabled ? (Rig.Settings.Lead === Layer.Slot ? ['LEAD', 'live'] : ['ON', 'live']) : ['OFF', '']));
    C.Slider(Emission, P, 'Count', { Label: 'Count', Minimum: 5000, Maximum: MaxCount, Step: 5000, Digits: 0, Labels: ['5k', '160k'] });
    C.Slider(Emission, P, 'Speed', { Label: 'Speed', Minimum: 0.1, Maximum: 3, Step: 0.05, Unit: '×' });

    const Shape = C.Card(Cards, 'Shape', 'SlidersHorizontal', { Index: 2 });
    C.Slider(Shape, P, 'Size', { Label: 'Size', Minimum: 0.3, Maximum: 3, Step: 0.05, Unit: '×' });
    C.Slider(Shape, P, 'Tail', { Label: 'Trail', Minimum: 0, Maximum: 0.4, Step: 0.01, Scale: 1000, Digits: 0, Unit: 'ms' });
    C.Slider(Shape, P, 'Turbulence', { Label: 'Turbulence', Minimum: 0, Maximum: 2.5, Step: 0.05 });
    if (Look.Variant) C.Segmented(Shape, P, 'Variant', Look.Variant.Options.map((Caption, Slot) => [Caption, Slot]), { Label: Look.Variant.Label });

    const Colour = C.Card(Cards, 'Colour', 'Palette', { Index: 3, Subtitle: 'Palette, then fine-tune each tone' });
    const Chips = Construct('div', 'palette-chips');
    const ShowChips = () => Chips.querySelectorAll('.palette-chip').forEach((Chip) => Chip.classList.toggle('active', Chip.dataset.palette === P.Palette));
    for (const [Name, Tones] of Object.entries(Palettes))
    {
        const Chip = Construct('button', 'palette-chip', Name);
        Chip.dataset.palette = Name;
        Chip.style.background = `linear-gradient(135deg, ${Tones[0]} 0 30%, ${Tones[1]} 30% 60%, ${Tones[2]} 60% 78%, ${Tones[3]} 78%)`;
        Chip.addEventListener('click', () => { Rig.AssignPalette(Layer, Name); C.Synchronise(); C.Notify('Palette', P); });
        Chips.appendChild(Chip);
    }
    Colour.appendChild(Chips);
    C.Bindings.push(ShowChips);
    ShowChips();
    ToneNames.forEach((Name, Slot) => C.Colour(Colour, P.Tones, Slot, { Label: Name, OnChange: () => { P.Palette = 'Custom'; ShowChips(); } }));

    const Lens = C.Card(Cards, 'Lens & intensity', 'Aperture', { Index: 4 });
    C.Slider(Lens, P, 'Aperture', { Label: 'Depth of field', Minimum: 0, Maximum: 0.3, Step: 0.01, Scale: 100, Digits: 0, Unit: 'cm lens', Labels: ['Pin-sharp', 'Bokeh'] });
    C.Slider(Lens, P, 'Gain', { Label: 'Intensity', Minimum: 0, Maximum: 2, Step: 0.05, Unit: '×' });

    const Timeline = C.Card(Cards, 'Timeline', 'Timer', { Index: 5 });
    C.Stats(Timeline, [['Cycle', () => Layer.Cycle, 's', 1], ['Fade', () => Layer.Fade, '', 2], ['Phase', () => Layer.Caption.replace(/<[^>]+>/g, '') || '–']], 3);
    const Actions = C.ActionRow(Timeline);
    C.Button(Actions, 'Restart cycle', 'RotateCcw', () => { Layer.Cycle = 0; });
    C.Button(Actions, 'Aim camera', 'Target', () => Rig.AimAt(Look.View, false));
    const More = C.ActionRow(Timeline);
    C.Button(More, 'Solo', 'Eye', () => { Solo(Layer.Slot); C.Synchronise(); C.Notify('Solo', P); });
    C.Button(More, 'Defaults', 'Undo2', () => { Rig.ApplyDefaults(Layer); C.Synchronise(); C.Notify('Defaults', P); }, 'danger');
}

function BuildCamera(C, Cards)
{
    const Turn = C.Card(Cards, 'Orbit', 'Orbit', { Index: 1, Subtitle: 'Drag the viewport to orbit, wheel to zoom' });
    C.Switch(Turn, Rig.Settings, 'Orbit', { Label: 'Auto orbit', Detail: 'Slow turn around the target (O)' });
    C.Slider(Turn, Rig.Goal, 'Yaw', { Label: 'Yaw', Minimum: -720, Maximum: 720, Step: 1, Digits: 0, Unit: '°' });
    C.Slider(Turn, Rig.Goal, 'Pitch', { Label: 'Pitch', Minimum: -2, Maximum: 80, Step: 1, Digits: 0, Unit: '°' });
    C.Slider(Turn, Rig.Goal, 'Distance', { Label: 'Distance', Minimum: 4, Maximum: 40, Step: 0.1, Digits: 1, Unit: 'm' });
    const Aim = C.Card(Cards, 'Framing', 'Target', { Index: 2 });
    const Pick = { View: Rig.Settings.Lead };
    C.Select(Aim, Pick, 'View', Looks.map((Look, Slot) => [`${Look.Name} view`, Slot]), { Label: 'Preset', OnChange: (Slot) => Rig.AimAt(Looks[Slot].View, false) });
    C.Stats(Aim, [['Yaw', () => Rig.Orbit.Yaw % 360, '°', 0], ['Pitch', () => Rig.Orbit.Pitch, '°', 0], ['Distance', () => Rig.Orbit.Distance, 'm', 1]], 3);
    C.Button(C.ActionRow(Aim), 'Aim at lead layer', 'Target', () => Rig.AimAt(Rig.Lead.Look.View, false), 'primary');
}

function BuildCar(C, Cards)
{
    const Body = C.Card(Cards, 'Body', 'Car', { Index: 1, Subtitle: 'Auto follows the lead layer' });
    C.Segmented(Body, Rig.Settings, 'Car', CarModes.map((Mode) => [Mode, Mode]));
    C.Receipt(Body, [['Showing', () => Rig.ShownCar], ['Lead layer', () => Rig.Lead.Look.Name]]);
    const Paint = C.Card(Cards, 'Paint', 'Palette', { Index: 2, Subtitle: 'Body coat; the vehicle transition restyles from one to the other' });
    C.Colour(Paint, Rig.Settings, 'PaintFrom', { Label: 'Coat' });
    C.Colour(Paint, Rig.Settings, 'PaintTo', { Label: 'Restyle to' });
}

const CustomBackdrop = ['#0b0920', '#010104'];
function BuildBackdrop(C, Cards)
{
    const Sky = C.Card(Cards, 'Gradient', 'Moon', { Index: 1 });
    const Follow = { On: !Rig.Settings.Backdrop };
    C.Switch(Sky, Follow, 'On', { Label: 'Follow lead layer', Detail: 'Each look carries its own sky', OnChange: (On) => { Rig.Settings.Backdrop = On ? null : CustomBackdrop; } });
    C.Colour(Sky, CustomBackdrop, 0, { Label: 'Top', OnChange: () => { Rig.Settings.Backdrop = CustomBackdrop; Follow.On = false; C.Synchronise(); } });
    C.Colour(Sky, CustomBackdrop, 1, { Label: 'Bottom', OnChange: () => { Rig.Settings.Backdrop = CustomBackdrop; Follow.On = false; C.Synchronise(); } });
}

function BuildPost(C, Cards)
{
    const Bloom = C.Card(Cards, 'Bloom', 'Sun', { Index: 1, Subtitle: 'Five-level down/up chain on the HDR scene' });
    C.Slider(Bloom, Rig.Settings, 'Glow', { Label: 'Strength', Minimum: 0, Maximum: 3, Step: 0.05 });
    C.Slider(Bloom, Rig.Settings, 'Cut', { Label: 'Threshold', Minimum: 0, Maximum: 3, Step: 0.05, Labels: ['Everything glows', 'Only hot cores'] });
    const Tone = C.Card(Cards, 'Tone', 'Aperture', { Index: 2 });
    C.Slider(Tone, Rig.Settings, 'Exposure', { Label: 'Exposure', Minimum: 0.2, Maximum: 3, Step: 0.05, Unit: '×' });
}

function BuildTime(C, Cards)
{
    const Clock = C.Card(Cards, 'Clock', 'Timer', { Index: 1, Subtitle: 'All motion is computed from this clock' });
    C.Metric(Clock, () => Rig.Clock, 's', 2, () => (Rig.Settings.Paused ? ['PAUSED', 'warm'] : ['RUNNING', 'live']));
    C.Switch(Clock, Rig.Settings, 'Paused', { Label: 'Paused', Detail: 'Freeze every layer (Space)' });
    const Jump = { At: 0 };
    C.Slider(Clock, Jump, 'At', { Label: 'Jump to', Minimum: 0, Maximum: 20, Step: 0.1, Digits: 1, Unit: 's', OnInput: (Seconds) => Rig.Seek(Seconds) });
    C.Button(C.ActionRow(Clock), 'Restart', 'RotateCcw', () => Rig.Seek(0));
}

function BuildRecipe(C, Cards)
{
    const Starts = C.Card(Cards, 'Built-in stacks', 'Layers', { Index: 1, Subtitle: 'Replace the scene with a ready stack' });
    for (const [Key, Recipe] of Object.entries(Stock))
        C.LinkButton(Starts, `${Recipe.Name} · ${Recipe.Layers.map((Look) => Looks.find((Each) => Each.Key === Look).Name).join(' + ')}`,
                     () => { LoadStock(Key); C.Synchronise(); C.Notify('Recipe', null); Announce(`Loaded ${Recipe.Name}`); });
    const Files = C.Card(Cards, 'File', 'Box', { Index: 2, Subtitle: 'Edits are kept in this browser automatically' });
    const Row = C.ActionRow(Files);
    C.Button(Row, 'Export JSON', 'Check', () => Download('Filament.recipe.json', JSON.stringify(WriteRecipe(), null, 2)), 'primary');
    C.Button(Row, 'Import', 'Plus', () => document.getElementById('recipe-file').click());
    C.Button(C.ActionRow(Files), 'Clear scene', 'Trash2', () => { for (const Layer of Rig.Layers) Layer.Enabled = false; C.Notify('Recipe', null); }, 'danger');
    C.Readout(Files, () => Rig.Layers.filter((Layer) => Layer.Enabled).map((Layer) => `${Layer.Look.Name.padEnd(20)} ${Format(Layer.Params.Count).padStart(5)}  ${Layer.Params.Palette}`));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 OUTLINER
//------------------------------------------------------------------------------------------------------------------------

const Controls = new EditorControls(() => { RefreshRows(); Persist(); });
let Selected = Entries.find((Entry) => Entry.Id === Query.get('select') || Entry.Id === `Layer:${Query.get('select')}`)?.Id ?? `Layer:${Rig.Lead.Look.Key}`;
const Collapsed = {};
const Rows = new Map();
let Search = '', Tree, VisibleCount, HiddenCount;

function ConstructOutliner()
{
    const Body = document.getElementById('outliner-body');
    ClearElement(Body);
    const Brand = Construct('div', 'brand');
    const Symbol = Construct('span', 'brand-symbol');
    Symbol.appendChild(ConstructGlyph('Sparkles', 24, 1.5));
    const Word = Construct('span', '', 'frontier');
    Word.appendChild(Construct('span', 'brand-dot', '.'));
    Brand.append(Symbol, Word, Construct('span', 'version', 'PARTICLES / 01'));
    Body.appendChild(Brand);
    const Label = Construct('div', 'scene-label', 'WORKSPACE');
    Label.appendChild(Construct('span', 'status-dot'));
    Body.appendChild(Label);
    const Title = Construct('div', 'scene-title', 'Filament');
    Title.appendChild(Construct('span', 'scene-extension', '.particles'));
    Body.appendChild(Title);

    const Tiles = Construct('div', 'count-tiles');
    const Tile = (ClassName, Glyph, Caption) =>
    {
        const Made = Construct('div', `count-tile ${ClassName}`);
        const Icon = Construct('span', 'count-icon');
        Icon.appendChild(ConstructGlyph(Glyph, 12, 2.4));
        const Count = Construct('span', 'count-value', '0');
        Made.append(Icon, Construct('span', 'count-caption', Caption), Count);
        Tiles.appendChild(Made);
        return Count;
    };
    VisibleCount = Tile('', 'Check', 'Layers on');
    HiddenCount = Tile('hidden-count', 'EyeOff', 'Layers off');
    Body.appendChild(Tiles);

    const Heading = Construct('div', 'outliner-heading');
    const H2 = Construct('h2', '', 'Outliner');
    H2.appendChild(Construct('span', '', String(Entries.length).padStart(2, '0')));
    Heading.appendChild(H2);
    Body.appendChild(Heading);

    const Find = Construct('label', 'search');
    Find.appendChild(ConstructGlyph('Search', 14, 1.7));
    const Input = document.createElement('input');
    Input.placeholder = 'Find a layer…';
    Input.addEventListener('input', () => { Search = String(Input.value || '').toLowerCase(); RenderTree(); });
    Find.appendChild(Input);
    Body.appendChild(Find);

    Tree = Construct('div', 'tree');
    Body.appendChild(Tree);

    const Bottom = Construct('div', 'outliner-bottom');
    const World = Construct('div', 'world-icon');
    World.appendChild(ConstructGlyph('Box', 18, 1.5));
    const Text = Construct('div');
    Text.append(Construct('strong', '', 'FilamentRig'), Construct('span', '', 'WebGL2 · one draw per layer'));
    Bottom.append(World, Text, Construct('span', 'status-dot'));
    Body.appendChild(Bottom);
    RenderTree();
}

function RenderTree()
{
    ClearElement(Tree);
    Rows.clear();
    const Matches = (Entry) => !Search || Entry.Name.toLowerCase().includes(Search) || Entry.Type.toLowerCase().includes(Search);
    for (const Group of GroupOrder)
    {
        const Members = Entries.filter((Entry) => Entry.Group === Group);
        const Shown = Members.filter(Matches);
        if (Shown.length === 0) continue;
        const Section = Construct('div', 'group');
        const Label = Construct('button', 'group-label');
        const Open = !Collapsed[Group] || !!Search;
        Label.append(ConstructGlyph(Open ? 'ChevronDown' : 'ChevronRight', 12, 1.8), Construct('span', '', Group), Construct('span', 'group-count', String(Members.length)));
        Label.addEventListener('click', () => { Collapsed[Group] = !Collapsed[Group]; RenderTree(); });
        Section.appendChild(Label);
        if (Open) for (const Entry of Shown) Section.appendChild(ConstructRow(Entry));
        Tree.appendChild(Section);
    }
    RefreshRows();
}

function ConstructRow(Entry)
{
    const Row = Construct('div', `tree-row${Entry.Id === Selected ? ' selected' : ''}`);
    const Button = Construct('button', 'object-button');
    Button.appendChild(ConstructGlyph(Entry.Glyph, 16, 1.6));
    const Meta = Construct('span', 'row-meta');
    Button.append(Construct('span', 'row-name', Entry.Name), Meta);
    Button.addEventListener('click', () => Select(Entry.Id));
    const Mark = Construct('span', 'row-state');
    Row.append(Button, Mark);
    let Eye = null;
    if (Entry.Toggle)
    {
        Eye = Construct('button', 'visibility');
        Eye.setAttribute('aria-label', `Toggle ${Entry.Name}`);
        Eye.addEventListener('click', () => { Entry.Toggle.Write(!Entry.Toggle.Read()); Controls.Synchronise(); RefreshRows(); Persist(); });
        Row.appendChild(Eye);
    }
    Rows.set(Entry.Id, { Row, Meta, Mark, Eye, Entry, EyeOn: null });
    return Row;
}

function RefreshRows()
{
    const On = Rig.Layers.filter((Layer) => Layer.Enabled).length;
    VisibleCount.textContent = String(On);
    HiddenCount.textContent = String(Rig.Layers.length - On);
    for (const Line of Rows.values())
    {
        const Visible = Line.Entry.Toggle ? !!Line.Entry.Toggle.Read() : true;
        AssignStyle(Line.Row, '--entity-colour', Line.Entry.Colour());
        Line.Meta.textContent = Line.Entry.Meta();
        Line.Mark.className = `row-state ${Visible ? Line.Entry.Mark() : ''}`;
        Line.Row.classList.toggle('is-off', !Visible);
        Line.Row.classList.toggle('selected', Line.Entry.Id === Selected);
        if (Line.Eye && Line.EyeOn !== Visible)
        {
            ClearElement(Line.Eye);
            Line.Eye.appendChild(ConstructGlyph(Visible ? 'Eye' : 'EyeOff', 13, 1.7));
            Line.EyeOn = Visible;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

function Select(Id)
{
    const Entry = Entries.find((Candidate) => Candidate.Id === Id) ?? Entries[0];
    Selected = Entry.Id;
    if (Entry.Layer && Entry.Layer.Enabled) Rig.Settings.Lead = Entry.Layer.Slot;
    ConstructInspector(Entry);
    RefreshRows();
}

function ConstructInspector(Entry)
{
    const Top = document.getElementById('inspector-top');
    ClearElement(Top);
    const Crumbs = Construct('div', 'crumbs', 'Inspector');
    Crumbs.append(ConstructGlyph('ChevronRight', 12, 1.8), Construct('span', '', Entry.Group));
    const Saved = Construct('div', 'save-status');
    Saved.append(ConstructGlyph('Check', 12, 2), Construct('span', '', 'Changes apply live'));
    Top.append(Crumbs, Saved);

    const Content = document.getElementById('inspector-content');
    ClearElement(Content);
    Controls.Reset();
    const Header = Construct('div', 'object-header');
    const Titles = Construct('div');
    Titles.append(Construct('div', 'eyebrow', Entry.Type), Construct('h1', '', Entry.Name));
    const Actions = Construct('div', 'object-actions');
    if (Entry.Toggle)
    {
        const Pill = Construct('button', 'enabled-pill');
        const Caption = Construct('em', '', '');
        Caption.style.fontStyle = 'normal';
        Pill.append(Construct('span'), Caption);
        const Show = () => { const On = !!Entry.Toggle.Read(); Pill.classList.toggle('disabled', !On); Caption.textContent = On ? 'Enabled' : 'Disabled'; };
        Pill.addEventListener('click', () => { Entry.Toggle.Write(!Entry.Toggle.Read()); Controls.Synchronise(); RefreshRows(); Persist(); });
        Show();
        Controls.Bindings.push(Show);
        Actions.appendChild(Pill);
    }
    Header.append(Titles, Actions);
    Content.appendChild(Header);
    const Section = Construct('div', 'section-label');
    Section.append(Construct('span', '', 'PROPERTIES'), Construct('span', '', Entry.Description || ''));
    Content.appendChild(Section);
    if (Entry.Layer)
    {
        const Layer = Entry.Layer;
        Controls.PropertySwitches(Content, [
            { Name: 'Visible', Glyph: 'Eye', Read: () => Layer.Enabled, Write: (On) => { Layer.Enabled = On; } },
            { Name: 'Lead', Glyph: 'Flag', Read: () => Rig.Settings.Lead === Layer.Slot, Write: (On) => { if (On) { Rig.Settings.Lead = Layer.Slot; Layer.Enabled = true; } } },
            { Name: 'Bokeh', Glyph: 'Aperture', Read: () => Layer.Params.Aperture > 0, Write: (On) => { Layer.Params.Aperture = On ? Math.max(0.06, Layer.Look.Defaults.Aperture ?? 0) : 0; } },
        ]);
    }
    const Cards = Construct('div', 'cards');
    Content.appendChild(Cards);
    Entry.Build(Controls, Cards);
    const Footer = Construct('footer', 'inspector-footer');
    const Left = Construct('span');
    Left.append(Construct('span', 'footer-dot'), Construct('span', '', 'Changes apply in real time'));
    Footer.append(Left, Construct('span', '', `${Entry.Name} / ${Entry.Type}`));
    Content.appendChild(Footer);
    Content.scrollTo?.(0, 0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                            VIEWPORT BAR, STATUS, HUD
//------------------------------------------------------------------------------------------------------------------------

const BarButtons = {};
function ConstructViewportBar()
{
    const Bar = document.getElementById('viewport-bar');
    ClearElement(Bar);
    const Heading = Construct('div', 'scene-heading');
    Heading.append(Construct('strong', '', 'Scene'), Construct('span', '', 'WebGL2 · GPU particles · bloom · depth of field'));
    Bar.append(Heading, Construct('span', 'live-pill', 'LIVE'));
    const LeadPick = document.createElement('select');
    LeadPick.className = 'select-field';
    LeadPick.style.width = '200px';
    LeadPick.title = 'Lead layer: drives the car body, backdrop and caption';
    Looks.forEach((Look, Slot) => { const Choice = document.createElement('option'); Choice.value = String(Slot); Choice.textContent = `Lead · ${Look.Name}`; LeadPick.appendChild(Choice); });
    LeadPick.addEventListener('change', () => { Rig.Settings.Lead = Number(LeadPick.value); Rig.Layers[Rig.Settings.Lead].Enabled = true; Controls.Synchronise(); RefreshRows(); Persist(); });
    BarButtons.Lead = LeadPick;
    const CarPick = document.createElement('select');
    CarPick.className = 'select-field';
    CarPick.style.width = '150px';
    CarModes.forEach((Mode) => { const Choice = document.createElement('option'); Choice.value = Mode; Choice.textContent = `Body · ${Mode}`; CarPick.appendChild(Choice); });
    CarPick.addEventListener('change', () => { Rig.Settings.Car = CarPick.value; Controls.Synchronise(); RefreshRows(); Persist(); });
    BarButtons.Car = CarPick;
    Bar.append(LeadPick, CarPick, Construct('span', 'bar-fill'));
    const Icon = (Key, Glyph, Title, Action) =>
    {
        const Button = Construct('button', 'icon-button');
        Button.title = Title;
        Button.appendChild(ConstructGlyph(Glyph, 15, 1.7));
        Button.addEventListener('click', Action);
        Bar.appendChild(Button);
        BarButtons[Key] = Button;
    };
    Icon('Pause', 'Pause', 'Pause / play (Space)', () => TogglePause());
    Icon('Restart', 'RotateCcw', 'Restart time (R)', () => Rig.Seek(0));
    Icon('Orbit', 'Orbit', 'Auto orbit (O)', () => { Rig.Settings.Orbit = !Rig.Settings.Orbit; Controls.Synchronise(); Persist(); });
    Icon('Hud', 'Activity', 'Readout (H)', () => ToggleHud());
    Icon('Clean', 'Scan', 'Clean view (V)', () => ToggleClean());
}

const StatusCells = {};
function ConstructStatus()
{
    const Status = document.getElementById('status');
    ClearElement(Status);
    const Cell = (Key, Label) =>
    {
        const Made = Construct('div', 'status-item');
        const Reading = Construct('strong', '', '–');
        Made.append(Construct('span', '', Label), Reading);
        Status.appendChild(Made);
        StatusCells[Key] = Reading;
    };
    Cell('Rate', 'REALTIME');
    Cell('Time', 'FRAME TIME');
    Cell('Particles', 'PARTICLES');
    Cell('Layers', 'LAYERS');
    Cell('Lead', 'LEAD');
    Cell('Clock', 'CLOCK');
    Cell('Camera', 'CAM');
    Status.appendChild(Construct('span', 'status-fill'));
    Cell('Adapter', 'GPU');
}

const Adapter = (() =>
{
    const Context = Rig.Renderer.getContext();
    const Debug = Context.getExtension('WEBGL_debug_renderer_info');
    return String(Debug ? Context.getParameter(Debug.UNMASKED_RENDERER_WEBGL) : Context.getParameter(Context.RENDERER)).replace(/\s*\(.*$/, '').slice(0, 48);
})();

let LastRefresh = 0, FrameSeconds = 1 / 60;
const CaptionChip = document.getElementById('caption-chip');
function Present(Now)
{
    if (Now - LastRefresh < RefreshInterval && !Capture) return;
    LastRefresh = Now;
    const Lead = Rig.Lead, On = Rig.Layers.filter((Layer) => Layer.Enabled);
    StatusCells.Rate.textContent = `${(1 / Math.max(FrameSeconds, 1e-4)).toFixed(0)} fps`;
    StatusCells.Time.textContent = `${(FrameSeconds * 1000).toFixed(1)} ms`;
    StatusCells.Particles.textContent = Rig.Particles().toLocaleString('en-US');
    StatusCells.Layers.textContent = `${On.length} / ${Rig.Layers.length}`;
    StatusCells.Lead.textContent = Lead.Look.Name;
    StatusCells.Clock.textContent = `${Rig.Clock.toFixed(1)} s${Rig.Settings.Paused ? ' · paused' : ''}`;
    StatusCells.Camera.textContent = `${(Rig.Orbit.Yaw % 360).toFixed(0)}° · ${Rig.Orbit.Pitch.toFixed(0)}° · ${Rig.Orbit.Distance.toFixed(1)} m`;
    StatusCells.Adapter.textContent = Adapter;
    document.getElementById('hud-text').textContent = [
        `lead      ${Lead.Look.Name}${Lead.Enabled ? '' : ' (off)'}`,
        ...On.map((Layer) => `layer     ${Layer.Look.Name.padEnd(19)} ${Format(Layer.Geometry.instanceCount).padStart(5)} · ${Layer.Params.Palette}`),
        `particles ${Rig.Particles().toLocaleString('en-US')} in ${On.length} draw${On.length === 1 ? '' : 's'}`,
        `clock     ${Rig.Clock.toFixed(2)} s · ${(1 / Math.max(FrameSeconds, 1e-4)).toFixed(0)} fps`,
    ].join('\n');
    const Caption = Lead.Enabled ? Lead.Caption : '';
    CaptionChip.innerHTML = Caption ? `<b>${Lead.Look.Name}</b> · ${Caption}` : '';
    CaptionChip.classList.toggle('hidden', !Caption);
    BarButtons.Lead.value = String(Rig.Settings.Lead);
    BarButtons.Car.value = Rig.Settings.Car;
    ClearElement(BarButtons.Pause);
    BarButtons.Pause.appendChild(ConstructGlyph(Rig.Settings.Paused ? 'Play' : 'Pause', 15, 1.7));
    BarButtons.Orbit.classList.toggle('active', Rig.Settings.Orbit);
    BarButtons.Hud.classList.toggle('active', !document.getElementById('hud').classList.contains('hidden'));
    Controls.RefreshLive();
    RefreshRows();
}

let NoticeTimer = 0;
function Announce(Text)
{
    const Notice = document.getElementById('event-notice');
    Notice.textContent = Text;
    Notice.classList.remove('hidden');
    clearTimeout(NoticeTimer);
    NoticeTimer = setTimeout(() => Notice.classList.add('hidden'), 2000);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 KEYS AND TOGGLES
//------------------------------------------------------------------------------------------------------------------------

const Resize = () => { const Box = Canvas.parentElement.getBoundingClientRect(); Rig.Resize(Math.max(1, Math.round(Box.width)), Math.max(1, Math.round(Box.height))); };
function TogglePause() { Rig.Settings.Paused = !Rig.Settings.Paused; Controls.Synchronise(); }
function ToggleHud() { document.getElementById('hud').classList.toggle('hidden'); }
function TogglePanels() { document.body.classList.toggle('panels-hidden'); requestAnimationFrame(Resize); }
function ToggleClean()
{
    const Clean = !document.body.classList.contains('Clean');
    document.body.classList.toggle('Clean', Clean);
    document.body.classList.toggle('panels-hidden', Clean);
    document.getElementById('hud').classList.toggle('hidden', Clean);
    requestAnimationFrame(Resize);
}
document.querySelectorAll('[data-panels]').forEach((Button) => Button.addEventListener('click', TogglePanels));
document.querySelector('[data-hud]').addEventListener('click', ToggleHud);
new ResizeObserver(Resize).observe(Canvas.parentElement);

window.addEventListener('keydown', (Event) =>
{
    const Tag = Event.target.tagName;
    if ((Tag === 'INPUT' && Event.target.type !== 'range') || Tag === 'SELECT' || Tag === 'TEXTAREA') return;
    const Digit = Number.parseInt(Event.key, 10);
    if (!Number.isNaN(Digit)) { const Slot = Digit === 0 ? 9 : Digit - 1; if (Rig.Layers[Slot]) Select(`Layer:${Rig.Layers[Slot].Look.Key}`); return; }
    const Key = Event.key.toLowerCase();
    if (Event.code === 'Space') { Event.preventDefault(); TogglePause(); }
    else if (Key === 'e') { const Entry = Entries.find((Each) => Each.Id === Selected); if (Entry?.Toggle) { Entry.Toggle.Write(!Entry.Toggle.Read()); Controls.Synchronise(); Persist(); } }
    else if (Key === 'r') Rig.Seek(0);
    else if (Key === 'o') { Rig.Settings.Orbit = !Rig.Settings.Orbit; Controls.Synchronise(); Persist(); }
    else if (Key === 'p') TogglePanels();
    else if (Key === 'v') ToggleClean();
    else if (Key === 'h') ToggleHud();
    else return;
    RefreshRows();
});

document.getElementById('recipe-file').addEventListener('change', async (Event) =>
{
    const File = Event.target.files?.[0];
    if (!File) return;
    try { ReadRecipe(JSON.parse(await File.text()), false); Select(Selected); Persist(); Announce(`Imported ${File.name}`); }
    catch (Failure) { Announce(`Import failed: ${Failure.message}`); }
    Event.target.value = '';
});

//------------------------------------------------------------------------------------------------------------------------
//                                                    START
//------------------------------------------------------------------------------------------------------------------------

let Restored = false;
const SoloSlot = Looks.findIndex((Look) => Look.Key === Query.get('solo'));
if (SoloSlot >= 0) { for (const Layer of Rig.Layers) Rig.ApplyDefaults(Layer); Solo(SoloSlot); Restored = true; }
else if (Query.has('recipe') && Stock[Query.get('recipe')]) LoadStock(Query.get('recipe'));
else if (Query.get('fresh') !== '1')
{
    try { const Saved = JSON.parse(localStorage.getItem(StorageKey) || 'null'); if (Saved) { ReadRecipe(Saved, true); Restored = true; } }
    catch (Failure) { Restored = false; }
}
if (!Restored && !Query.has('recipe')) LoadStock('Launch');
if (SoloSlot >= 0) Restored = false;
Rig.AimAt({ ...Rig.Goal, Target: Rig.Goal.Target.toArray() }, true);
Selected = Entries.find((Entry) => Entry.Id === Query.get('select') || Entry.Id === `Layer:${Query.get('select')}`)?.Id ?? `Layer:${Rig.Lead.Look.Key}`;

ConstructOutliner();
ConstructViewportBar();
ConstructStatus();
Select(Selected);
Resize();

let Last = performance.now();
function Loop(Now)
{
    const Seconds = Math.min(0.05, (Now - Last) / 1000);
    FrameSeconds += (Math.max(1e-4, (Now - Last) / 1000) - FrameSeconds) * 0.1;
    Last = Now;
    Rig.Advance(Seconds);
    Present(Now);
    requestAnimationFrame(Loop);
}

// Capture hook for the headless checks.
window.ParticleEditor = {
    Rig, Select, LoadStock, Solo, WriteRecipe, ReadRecipe,
    Seek(Seconds) { Rig.Seek(Seconds); Present(performance.now()); },
};
Rig.Seek(Number(Query.get('time') ?? 0));
Present(performance.now());
BringUp.classList.add('hidden');
if (Restored) Announce('Restored your last scene');
if (!Capture) requestAnimationFrame(Loop);
document.body.classList.add('Ready');

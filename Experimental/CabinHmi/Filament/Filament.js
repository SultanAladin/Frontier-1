// FILAMENT.JS
// 🧩 Particle showroom: the ControlVehicle on a dark stage with ten GPU particle looks seen on new cars' screens and
//    in motion-graphics tools: light-speed streaks, a dust "digital twin" that assembles the car, wind-tunnel airflow,
//    Particular-style emitters, a MoGraph clone field, a charging flow, and (from reference stills) light threads
//    orbiting a glass sphere, particle flowers with bokeh, a vehicle transition and a fibre-optic wave. Orbit the camera, tune count, speed,
//    trail, size, glow, turbulence and palette, and switch how the car body shows.
// 📝 The renderer lives in FilamentRig.js (shared with the particle editor); this page shows one layer at a time.
//    URL options (for captures): ?look=Airflow&time=6&palette=Ice&variant=1&car=Ghost&count=80000&aperture=0.1&still=1&ui=0&capture=1

import { RaiseRig, Looks, Palettes, CarModes, MaxCount } from '../../ParticleEditor/Source/Particles/FilamentRig.js';

const Query = new URLSearchParams(location.search);
const Capture = Query.get('capture') === '1';                       // headless captures: no animation loop, one frame per Seek
const Rig = await RaiseRig(document.getElementById('Scene'), { Capture });

//------------------------------------------------------------------------------------------------------------------------
//                                                    SETTINGS
//------------------------------------------------------------------------------------------------------------------------

// The panel edits the shown layer's parameters; Glow, Car, Orbit and Paused belong to the whole set.
const Settings = {
    Look: Math.max(0, Looks.findIndex((Each) => Each.Key === Query.get('look'))),
    get Count() { return Shown().Params.Count; }, set Count(V) { Shown().Params.Count = V; },
    get Speed() { return Shown().Params.Speed; }, set Speed(V) { Shown().Params.Speed = V; },
    get Tail() { return Shown().Params.Tail; }, set Tail(V) { Shown().Params.Tail = V; },
    get Size() { return Shown().Params.Size; }, set Size(V) { Shown().Params.Size = V; },
    get Turbulence() { return Shown().Params.Turbulence; }, set Turbulence(V) { Shown().Params.Turbulence = V; },
    get Aperture() { return Shown().Params.Aperture; }, set Aperture(V) { Shown().Params.Aperture = V; },
    get Variant() { return Shown().Params.Variant; }, set Variant(V) { Shown().Params.Variant = V; },
    get Palette() { return Shown().Params.Palette; },
    get Glow() { return Rig.Settings.Glow; }, set Glow(V) { Rig.Settings.Glow = V; },
    get Car() { return Rig.Settings.Car; }, set Car(V) { Rig.Settings.Car = V; },
    get Orbit() { return Rig.Settings.Orbit; }, set Orbit(V) { Rig.Settings.Orbit = V; },
    get Paused() { return Rig.Settings.Paused; }, set Paused(V) { Rig.Settings.Paused = V; },
};
const Shown = () => Rig.Layers[Settings.Look];
Settings.Orbit = Query.get('orbit') === '1';
Settings.Paused = Query.get('still') === '1';

const Sliders = [
    { Key: 'Count',      Label: 'Particles',  Min: 5000, Max: MaxCount, Step: 5000, Show: (V) => `${Math.round(V / 1000)}k` },
    { Key: 'Speed',      Label: 'Speed',      Min: 0.1,  Max: 3,   Step: 0.05, Show: (V) => `${V.toFixed(2)}×` },
    { Key: 'Tail',       Label: 'Trail',      Min: 0,    Max: 0.4, Step: 0.01, Show: (V) => `${Math.round(V * 1000)} ms` },
    { Key: 'Size',       Label: 'Size',       Min: 0.3,  Max: 3,   Step: 0.05, Show: (V) => `${V.toFixed(2)}×` },
    { Key: 'Glow',       Label: 'Glow',       Min: 0,    Max: 3,   Step: 0.05, Show: (V) => `${V.toFixed(2)}` },
    { Key: 'Turbulence', Label: 'Turbulence', Min: 0,    Max: 2.5, Step: 0.05, Show: (V) => `${V.toFixed(2)}` },
    { Key: 'Aperture',   Label: 'Depth of field', Min: 0, Max: 0.3, Step: 0.01, Show: (V) => (V > 0 ? `${Math.round(V * 100)} cm lens` : 'Off') },
];

//------------------------------------------------------------------------------------------------------------------------
//                                                    PANEL
//------------------------------------------------------------------------------------------------------------------------

const Build = (Tag, Class, Html) => { const Made = document.createElement(Tag); if (Class) Made.className = Class; if (Html !== undefined) Made.innerHTML = Html; return Made; };
const Controls = document.getElementById('Controls');
const Readings = {};
for (const Slider of Sliders)
{
    const Row = Build('label', 'Control');
    const Head = Build('header', '', `<span>${Slider.Label}</span><output></output>`);
    const Input = Build('input');
    Object.assign(Input, { type: 'range', min: Slider.Min, max: Slider.Max, step: Slider.Step });
    Input.addEventListener('input', () => { Settings[Slider.Key] = +Input.value; Head.querySelector('output').textContent = Slider.Show(+Input.value); });
    Row.append(Head, Input);
    Controls.append(Row);
    Readings[Slider.Key] = { Input, Output: Head.querySelector('output'), Slider };
}
function Segments(Host, Label, Options, Pick)
{
    Host.replaceChildren();
    if (!Options) return;
    const Row = Build('div', 'Control', `<header><span>${Label}</span></header>`);
    const Bar = Build('div', 'Segments');
    Options.forEach((Option, Slot) =>
    {
        const Button = Build('button', '', Option);
        Button.addEventListener('click', () => { Pick(Slot); Bar.querySelectorAll('button').forEach((Each, Other) => Each.classList.toggle('On', Other === Slot)); });
        Bar.append(Button);
    });
    Row.append(Bar);
    Host.append(Row);
    return Bar;
}
const CarBar = Segments(document.getElementById('CarRow'), 'Car body', CarModes, (Slot) => { Settings.Car = CarModes[Slot]; });
const PaletteRow = document.getElementById('Palettes');
for (const [Name, Tones] of Object.entries(Palettes))
{
    const Swatch = Build('button', 'Swatch');
    Swatch.title = Name;
    Swatch.style.background = `linear-gradient(135deg, ${Tones[0]} 0 34%, ${Tones[1]} 34% 67%, ${Tones[3]} 67%)`;
    Swatch.dataset.palette = Name;
    Swatch.addEventListener('click', () => SetPalette(Name));
    PaletteRow.append(Swatch);
}
function SetPalette(Name)
{
    Rig.AssignPalette(Shown(), Name);
    PaletteRow.querySelectorAll('.Swatch').forEach((Each) => Each.classList.toggle('On', Each.dataset.palette === Name));
}
function SyncPanel()
{
    for (const { Input, Output, Slider } of Object.values(Readings)) { Input.value = Settings[Slider.Key]; Output.textContent = Slider.Show(Settings[Slider.Key]); }
    CarBar.querySelectorAll('button').forEach((Each, Slot) => Each.classList.toggle('On', CarModes[Slot] === Settings.Car));
}
const OrbitButton = document.getElementById('Orbit'), PauseButton = document.getElementById('Pause');
const SyncToggles = () => { OrbitButton.classList.toggle('On', Settings.Orbit); PauseButton.textContent = Settings.Paused ? 'Play' : 'Pause'; };
OrbitButton.addEventListener('click', () => { Settings.Orbit = !Settings.Orbit; SyncToggles(); });
PauseButton.addEventListener('click', () => { Settings.Paused = !Settings.Paused; SyncToggles(); });
document.getElementById('Reset').addEventListener('click', () => SetLook(Settings.Look, true));

const Strip = document.getElementById('Strip');
Looks.forEach((Look, Slot) =>
{
    const Button = Build('button', 'Pick', `<i>${(Slot + 1) % 10}</i><b>${Look.Name}</b>`);
    Button.addEventListener('click', () => SetLook(Slot));
    Strip.append(Button);
});

function SetLook(Slot, Reapply = Slot !== Settings.Look || !SetLook.Done)
{
    const Look = Looks[Slot];
    Settings.Look = Slot;
    Rig.Settings.Lead = Slot;
    Rig.Layers.forEach((Layer) => { Layer.Enabled = Layer.Slot === Slot; });
    if (Reapply)
    {
        Rig.ApplyDefaults(Shown());
        SetPalette(Look.Defaults.Palette);
        Rig.AimAt(Look.View, !SetLook.Done);
    }
    SetLook.Done = true;
    document.getElementById('LookName').textContent = Look.Name;
    document.getElementById('LookTagline').textContent = Look.Tagline;
    Strip.querySelectorAll('.Pick').forEach((Each, Other) => Each.classList.toggle('On', Other === Slot));
    const VariantBar = Segments(document.getElementById('VariantRow'), Look.Variant?.Label, Look.Variant?.Options, (Pick) => { Settings.Variant = Pick; });
    VariantBar?.querySelectorAll('button').forEach((Each, Other) => Each.classList.toggle('On', Other === Settings.Variant));
    SyncPanel();
}

window.addEventListener('keydown', (Event) =>
{
    if (Event.target.tagName === 'INPUT' && Event.target.type !== 'range') return;
    const Digit = Number.parseInt(Event.key, 10);
    if (Digit === 0 && Looks.length >= 10) SetLook(9);
    else if (Digit >= 1 && Digit <= Looks.length) SetLook(Digit - 1);
    else if (Event.code === 'Space') { Event.preventDefault(); Settings.Paused = !Settings.Paused; SyncToggles(); }
    else if (Event.key === 'h' || Event.key === 'H') document.body.classList.toggle('Bare');
    else if (Event.key === 'o' || Event.key === 'O') { Settings.Orbit = !Settings.Orbit; SyncToggles(); }
});

//------------------------------------------------------------------------------------------------------------------------
//                                                    LOOP
//------------------------------------------------------------------------------------------------------------------------

const Readout = document.getElementById('Readout');
function Advance(Seconds)
{
    Rig.Advance(Seconds);
    const Text = Shown().Caption;
    Readout.innerHTML = Text;
    Readout.classList.toggle('On', Text !== '');
}
const Resize = () => Rig.Resize(window.innerWidth, window.innerHeight);
window.addEventListener('resize', Resize);

let Last = performance.now();
function Loop(Now)
{
    const Seconds = Math.min(0.05, (Now - Last) / 1000);
    Last = Now;
    Advance(Seconds);
    requestAnimationFrame(Loop);
}

// Capture hook: jump straight to a moment (runs the timelines in 1/30 s steps so cycles land where they would live).
window.Filament = {
    Looks: Looks.map((Each) => Each.Key),
    SetLook: (Key) => SetLook(Math.max(0, Looks.findIndex((Each) => Each.Key === Key))),
    Seek(Seconds) { Rig.Seek(Seconds); Advance(0); },
    Settings,
};

Resize();
SetLook(Settings.Look);
for (const Key of ['Count', 'Speed', 'Tail', 'Size', 'Glow', 'Turbulence', 'Aperture']) if (Query.has(Key.toLowerCase())) Settings[Key] = +Query.get(Key.toLowerCase());
if (Query.has('palette') && Palettes[Query.get('palette')]) SetPalette(Query.get('palette'));
if (Query.has('car') && CarModes.includes(Query.get('car'))) Settings.Car = Query.get('car');
if (Query.has('variant')) Settings.Variant = +Query.get('variant');
if (Query.get('ui') === '0') document.body.classList.add('Bare');
SyncPanel(); SyncToggles();
SetLook(Settings.Look, false);
if (Query.has('time')) window.Filament.Seek(+Query.get('time'));
if (!Capture) requestAnimationFrame(Loop);
else if (!Query.has('time')) window.Filament.Seek(0);
document.body.classList.add('Ready');

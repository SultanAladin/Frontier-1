// CLUSTER DECK
// 🧩 Runtime shared by the three dashboard clusters (the display behind the steering wheel, separate from the
//    centre HMI). It owns the drive modes (Cruise · Eco · Sport · Race), the steering-wheel control strip, the
//    side-panel catalogue (fuel, trip, route, coach, engine, lap, tyres, …), shift lights, tell-tales and the cabin
//    link to the centre display of the same car. Each cluster page brings its own core gauge, layout and styling.
// 📝 Markup contract inside #Dash:
//      [data-panel="Fuel|Trip|Route|Media|Coach|EcoHistory|Engine|GForce|Revs|Lap|Tyres|…"]  filled and updated here
//      .Group[data-modes="Cruise Eco"]   panel group shown only in the listed modes (cross-fades on change)
//      #ShiftLights #Telltales #ModeWash #ModeName #ModeLine #DashClock #OdoValue #OutsideText   optional
//    The wheel strip is generated into #Wheel. Keys: ↑ ↓ pedals, Q E paddles, P R N D M, 1–4 drive modes.

import { FitDisplay, ClockText } from '../Shared/DisplayKit.js';
import { BindPowertrainKeys, BindHold } from '../Shared/Powertrain.js';
import { LampEcho, LampGlyphs, SyncTelltales } from '../Shared/LampRig.js';
import { OpenLink } from '../Shared/CabinLink.js';

export const ModeOrder = ['Cruise', 'Eco', 'Sport', 'Race'];
export const DriveModes = {
    Cruise: { Style: 'Comfort', Line: 'Relaxed shifts · route ahead' },
    Eco:    { Style: 'Eco',     Line: 'Early shifts · gentle throttle · coach' },
    Sport:  { Style: 'Sport',   Line: 'Held gears · sharper throttle · boost' },
    Race:   { Style: 'Track',   Line: 'Shift lights · lap timer · tyre heat' },
};

const Clamp = (Value, Low, High) => Math.min(High, Math.max(Low, Value));
const Glyph = {
    Fuel:   '<svg viewBox="0 0 24 24"><path d="M3 21V5a2 2 0 0 1 2-2h7a2 2 0 0 1 2 2v16M2 21h13M5.5 7h6M14 9h2.5a1.5 1.5 0 0 1 1.5 1.5V17a1.5 1.5 0 0 0 3 0V8l-3-3"/></svg>',
    Right:  '<svg viewBox="0 0 48 48"><path d="M16 44V24a6 6 0 0 1 6-6h14"/><path d="M30 10l8 8-8 8"/></svg>',
    Left:   '<svg viewBox="0 0 48 48"><path d="M32 44V24a6 6 0 0 0-6-6H12"/><path d="M18 10l-8 8 8 8"/></svg>',
    Ahead:  '<svg viewBox="0 0 48 48"><path d="M24 44V8"/><path d="M15 17l9-9 9 9"/></svg>',
    Note:   '<svg viewBox="0 0 24 24"><path d="M9 18V5l11-2v13"/><circle cx="6.5" cy="18" r="2.5"/><circle cx="17.5" cy="16" r="2.5"/></svg>',
    Leaf:   '<svg viewBox="0 0 24 24"><path d="M4 20c0-9 6-15 16-16 0 10-6 16-15 16M4 20l9-9"/></svg>',
    Flag:   '<svg viewBox="0 0 24 24"><path d="M5 21V4M5 4h13l-3 4 3 4H5"/></svg>',
};
const Telltales = [
    ['Left', 'Green'], ['Low', 'Green'], ['High', 'Blue'], ['Auto', 'Green'], ['FogFront', 'Green'], ['FogRear', 'Amber'],
    ['Park', 'Red'], ['FuelLow', 'Amber'], ['Hazard', 'Red'], ['Right', 'Green'],
];
const Route = [
    { Turn: 'Right', Road: 'Marina Drive', Metres: 820 },
    { Turn: 'Ahead', Road: 'N2 Coast Road', Metres: 4200 },
    { Turn: 'Left',  Road: 'Harbour Lane', Metres: 640 },
    { Turn: 'Right', Road: 'Lighthouse Way', Metres: 1350 },
];
const LapLength = 3.2;                                              // [km]
const Time = (Seconds, Places = 1) => { const M = Math.floor(Seconds / 60), S = Seconds - M * 60; return `${M}:${S.toFixed(Places).padStart(Places ? 3 + Places : 2, '0')}`; };
const Distance = (Metres) => (Metres >= 1000 ? `${(Metres / 1000).toFixed(1)} km` : `${Math.max(0, Math.round(Metres / 10) * 10)} m`);

//------------------------------------------------------------------------------------------------------------------------
//                                                    PANELS
//------------------------------------------------------------------------------------------------------------------------

// Each panel: (Root, Train, Deck) → Update(). Refs are [data-ref] children; text writes only on change.
function Refs(Root)
{
    const Map = {};
    Root.querySelectorAll('[data-ref]').forEach((Each) => { Map[Each.dataset.ref] = Each; });
    const Shown = {};
    Map.Put = (Key, Text) => { if (Shown[Key] !== Text) { Shown[Key] = Text; Map[Key].textContent = Text; } };
    return Map;
}

const Panels = {
    Fuel(Root, Train)
    {
        Root.innerHTML = `
            <div class="PanelHead"><span class="Glyph">${Glyph.Fuel}</span><em>Fuel</em><b data-ref="Litres">0</b><small>L</small></div>
            <div class="FuelTrack"><i data-ref="Bar"></i><span style="left:25%"></span><span style="left:50%"></span><span style="left:75%"></span></div>
            <div class="FuelFoot"><span>E</span><span class="Range">Range <b data-ref="Range">0</b> km</span><span>F</span></div>`;
        const R = Refs(Root);
        return () =>
        {
            const Level = Math.max(Train.Fuel, Train.SweepLevel);
            R.Bar.style.width = `${(Level * 100).toFixed(1)}%`;
            R.Put('Litres', Train.Litres.toFixed(1));
            R.Put('Range', String(Math.round(Train.Range)));
            Root.classList.toggle('Low', Train.Fuel < 0.12);
        };
    },

    Trip(Root, Train, Deck)
    {
        Root.innerHTML = `
            <div class="PanelHead"><em>Trip A</em></div>
            <div class="Stats">
                <div><small>Distance</small><b data-ref="Trip">0</b><i>km</i></div>
                <div><small>Average</small><b data-ref="Average">0</b><i>L/100 km</i></div>
                <div><small>Avg speed</small><b data-ref="AvgSpeed">0</b><i>km/h</i></div>
                <div><small>Drive time</small><b data-ref="Clock">0</b><i>h:min</i></div>
            </div>`;
        const R = Refs(Root);
        return () =>
        {
            R.Put('Trip', Train.Trip.toFixed(1));
            R.Put('Average', Train.Average.toFixed(1));
            R.Put('AvgSpeed', String(Math.round(Train.Trip / Math.max(0.01, Deck.DriveSeconds / 3600))));
            R.Put('Clock', `${Math.floor(Deck.DriveSeconds / 3600)}:${String(Math.floor(Deck.DriveSeconds / 60) % 60).padStart(2, '0')}`);
        };
    },

    Route(Root, Train, Deck)
    {
        Root.innerHTML = `
            <div class="PanelHead"><em>Route</em><small data-ref="Eta">ETA</small></div>
            <div class="Turn"><span class="Arrow" data-ref="Arrow"></span><div><b data-ref="Next">0 m</b><span data-ref="Road">Road</span></div></div>
            <div class="Progress"><i data-ref="Bar"></i></div>
            <div class="Then" data-ref="Then">Then</div>`;
        const R = Refs(Root);
        let ShownStep = -1;
        return () =>
        {
            const Step = Route[Deck.Route.Step], Left = Deck.Route.Left;
            if (ShownStep !== Deck.Route.Step)
            {
                ShownStep = Deck.Route.Step;
                R.Arrow.innerHTML = Glyph[Step.Turn];
                R.Arrow.classList.remove('Pop'); void R.Arrow.offsetWidth; R.Arrow.classList.add('Pop');
                R.Put('Road', Step.Road);
                const After = Route[(Deck.Route.Step + 1) % Route.length];
                R.Put('Then', `Then ${After.Turn === 'Ahead' ? 'straight on' : After.Turn.toLowerCase()} · ${After.Road}`);
            }
            R.Put('Next', Distance(Left));
            R.Bar.style.width = `${(100 * (1 - Left / Step.Metres)).toFixed(1)}%`;
            const Remaining = Left + Route.slice(Deck.Route.Step + 1).reduce((Sum, Each) => Sum + Each.Metres, 0);
            const Eta = new Date(Date.now() + (Remaining / 1000) / Math.max(30, Train.Kmh || 48) * 3600 * 1000);
            R.Put('Eta', `ETA ${ClockText(Eta)} · ${(Remaining / 1000).toFixed(1)} km`);
        };
    },

    Media(Root, Train, Deck)
    {
        const Song = Deck.Song;
        Root.innerHTML = `
            <div class="PanelHead"><span class="Glyph">${Glyph.Note}</span><em>Now playing</em></div>
            <b class="Song">${Song.Title}</b><span class="Artist">${Song.Artist}</span>
            <div class="Progress"><i data-ref="Bar"></i></div>
            <div class="Times"><span data-ref="At">0:00</span><span>${Time(Song.Length, 0)}</span></div>`;
        const R = Refs(Root);
        const Began = performance.now() - 61000;
        return () =>
        {
            const At = ((performance.now() - Began) / 1000) % Song.Length;
            R.Bar.style.width = `${(At / Song.Length * 100).toFixed(1)}%`;
            R.Put('At', Time(At, 0));
        };
    },

    Coach(Root, Train)
    {
        Root.innerHTML = `
            <div class="PanelHead"><span class="Glyph">${Glyph.Leaf}</span><em>Eco coach</em><b data-ref="Score">0</b><small>score</small></div>
            <div class="Instant"><b data-ref="Instant">0.0</b><i data-ref="Unit">L/100 km</i></div>
            <div class="CoachScale"><span class="Zone" data-ref="Zone"></span><i data-ref="Marker"></i></div>
            <div class="CoachTicks"><span>0</span><span>10</span><span>20</span></div>
            <div class="Hint" data-ref="Hint">Hint</div>`;
        const R = Refs(Root);
        let Score = 80, Shown = 0;
        return () =>
        {
            Score += (Train.Eco * 100 - Score) * 0.02;
            R.Put('Score', String(Math.round(Score)));
            const Instant = Train.Instant;
            const Target  = Train.Average * 0.95;
            R.Zone.style.width = `${Clamp(Target / 20, 0, 1) * 100}%`;
            if (Instant === null) { R.Put('Instant', Train.Flow.toFixed(1)); R.Put('Unit', 'L/h · standing'); Shown += (0 - Shown) * 0.2; }
            else { R.Put('Instant', Instant.toFixed(1)); R.Put('Unit', 'L/100 km'); Shown += (Instant - Shown) * 0.2; }
            R.Marker.style.left = `${Clamp(Shown / 20, 0, 1) * 100}%`;
            const Hint = Instant === null ? 'Standing · engine idles' : Train.Throttle < 0.04 ? 'Coasting · zero fuel' : Instant <= Target ? 'In the zone · hold it' : Instant < Target * 1.6 ? 'Ease off a little' : 'Heavy throttle · shift up early';
            R.Put('Hint', Hint);
            Root.classList.toggle('Over', Instant !== null && Instant > Target * 1.6);
        };
    },

    EcoHistory(Root, Train)
    {
        const Count = 28;
        Root.innerHTML = `
            <div class="PanelHead"><em>Consumption</em><small>last 40 s</small></div>
            <div class="Bars">${Array.from({ length: Count }, () => '<i></i>').join('')}<span class="Mean" data-ref="Mean"></span></div>
            <div class="Legend"><span>Average <b data-ref="Average">0</b> L/100 km</span></div>`;
        const R = Refs(Root);
        const Bars = [...Root.querySelectorAll('.Bars i')];
        const Samples = Array.from({ length: Count }, (_, Index) => 5 + 3 * Math.sin(Index * 0.7) + Index % 3);
        let Last = 0;
        return () =>
        {
            const Now = performance.now();
            if (Now - Last > 1400)
            {
                Last = Now;
                Samples.push(Train.Instant ?? 0);
                Samples.shift();
                Bars.forEach((Bar, Index) =>
                {
                    const Value = Samples[Index];
                    Bar.style.height = `${Clamp(Value / 20, 0.03, 1) * 100}%`;
                    Bar.className = Value === 0 ? 'Zero' : Value <= Train.Average ? 'Good' : 'Over';
                });
            }
            R.Mean.style.bottom = `${Clamp(Train.Average / 20, 0, 1) * 100}%`;
            R.Put('Average', Train.Average.toFixed(1));
        };
    },

    Engine(Root, Train, Deck)
    {
        const Row = (Key, Label, Unit) => `<div class="Row"><small>${Label}</small><b data-ref="${Key}">0</b><i>${Unit}</i><div class="Meter"><span data-ref="${Key}Bar"></span></div></div>`;
        Root.innerHTML = `<div class="PanelHead"><em>Engine</em><small data-ref="Style">Sport map</small></div>
            ${Row('Boost', 'Boost', 'bar')}${Row('Torque', 'Torque', 'Nm')}${Row('Coolant', 'Coolant', '°C')}${Row('Oil', 'Oil', '°C')}`;
        const R = Refs(Root);
        let Oil = 82;
        return () =>
        {
            Oil += ((Train.Coolant + 6 + Train.Throttle * 14) - Oil) * 0.004;
            const Torque = Train.Torque(Train.Rpm) * Train.Throttle;
            R.Put('Boost', Train.Boost.toFixed(2));   R.BoostBar.style.width = `${Clamp(Train.Boost / 2.8, 0, 1) * 100}%`;
            R.Put('Torque', String(Math.round(Torque))); R.TorqueBar.style.width = `${Clamp(Torque / Train.PeakTorque, 0, 1) * 100}%`;
            R.Put('Coolant', String(Math.round(Train.Coolant))); R.CoolantBar.style.width = `${Clamp((Train.Coolant - 40) / 80, 0, 1) * 100}%`;
            R.Put('Oil', String(Math.round(Oil)));    R.OilBar.style.width = `${Clamp((Oil - 40) / 90, 0, 1) * 100}%`;
            R.Put('Style', `${Deck.Mode} map`);
        };
    },

    GForce(Root, Train)
    {
        const Size = 220, Mid = Size / 2, Scale = 80;
        Root.innerHTML = `
            <div class="PanelHead"><em>G force</em><small>max <b data-ref="Max">0.00</b> g</small></div>
            <svg class="GPlot" viewBox="0 0 ${Size} ${Size}">
                <circle cx="${Mid}" cy="${Mid}" r="${Scale}" class="Ring"/><circle cx="${Mid}" cy="${Mid}" r="${Scale / 2}" class="Ring"/>
                <line x1="${Mid}" y1="14" x2="${Mid}" y2="${Size - 14}" class="Axis"/><line x1="14" y1="${Mid}" x2="${Size - 14}" y2="${Mid}" class="Axis"/>
                <polyline class="Trail" data-ref="Trail" points=""/>
                <circle class="Dot" data-ref="Dot" r="7" cx="${Mid}" cy="${Mid}"/>
            </svg>
            <div class="GRead"><span>Long <b data-ref="Long">0.00</b></span><span>Lat <b data-ref="Lat">0.00</b></span></div>`;
        const R = Refs(Root);
        const Trail = [];
        let Last = 0;
        return () =>
        {
            const X = Mid + Clamp(Train.GLat, -1.3, 1.3) * Scale, Y = Mid + Clamp(Train.GLong, -1.3, 1.3) * Scale;
            R.Dot.setAttribute('cx', X.toFixed(1)); R.Dot.setAttribute('cy', Y.toFixed(1));
            const Now = performance.now();
            if (Now - Last > 80) { Last = Now; Trail.push(`${X.toFixed(1)},${Y.toFixed(1)}`); if (Trail.length > 30) Trail.shift(); R.Trail.setAttribute('points', Trail.join(' ')); }
            R.Put('Max', Train.GMax.toFixed(2)); R.Put('Long', Train.GLong.toFixed(2)); R.Put('Lat', Train.GLat.toFixed(2));
        };
    },

    Revs(Root, Train)
    {
        const Count = 30;
        Root.innerHTML = `
            <div class="PanelHead"><em>Engine speed</em><small><b data-ref="Rpm">0</b> rpm</small></div>
            <div class="RevBar">${Array.from({ length: Count }, (_, Index) => `<i class="${(Index + 1) / Count * Train.Limit > Train.Redline ? 'Red' : ''}"></i>`).join('')}</div>
            <div class="RevFoot"><span>0</span><span>${Math.round(Train.Limit / 1000)}k</span></div>`;
        const R = Refs(Root);
        const Cells = [...Root.querySelectorAll('.RevBar i')];
        return () =>
        {
            const Rpm = Math.max(Train.Rpm, Train.SweepLevel * Train.Limit);
            const Lit = Math.round(Rpm / Train.Limit * Count);
            Cells.forEach((Cell, Index) => Cell.classList.toggle('Lit', Index < Lit));
            R.Put('Rpm', String(Math.round(Rpm / 50) * 50));
        };
    },

    Lap(Root, Train, Deck)
    {
        Root.innerHTML = `
            <div class="PanelHead"><span class="Glyph">${Glyph.Flag}</span><em>Lap <b data-ref="Count">1</b></em><small>${LapLength} km circuit</small></div>
            <div class="LapNow" data-ref="Now">0:00.0</div>
            <div class="Delta" data-ref="Delta">+0.00</div>
            <div class="LapProgress"><i data-ref="Bar"></i></div>
            <div class="LapRows"><span>Last <b data-ref="Last">–</b></span><span>Best <b data-ref="Best">0</b></span></div>`;
        const R = Refs(Root);
        return () =>
        {
            const Lap = Deck.Lap, Share = Lap.Metres / (LapLength * 1000);
            R.Put('Count', String(Lap.Count));
            R.Put('Now', Time(Lap.Clock, 1));
            const Delta = Lap.Clock - Lap.Best * Share;
            R.Put('Delta', `${Delta >= 0 ? '+' : '−'}${Math.abs(Delta).toFixed(2)}`);
            R.Delta.classList.toggle('Ahead', Delta < 0);
            R.Bar.style.width = `${(Share * 100).toFixed(1)}%`;
            R.Put('Last', Lap.Last ? Time(Lap.Last, 2) : '–');
            R.Put('Best', Time(Lap.Best, 2));
        };
    },

    Tyres(Root, Train, Deck)
    {
        const Corners = [['FL', 'Front left'], ['FR', 'Front right'], ['RL', 'Rear left'], ['RR', 'Rear right']];
        Root.innerHTML = `
            <div class="PanelHead"><em>Tyres</em><small>°C · bar</small></div>
            <div class="TyreMap">
                <svg class="CarTop" viewBox="0 0 60 120"><path d="M14 8 Q30 0 46 8 L50 40 L50 100 Q30 120 10 100 L10 40Z"/><path d="M16 34 H44 M16 84 H44"/></svg>
                ${Corners.map(([Key, Label]) => `<div class="Tyre ${Key}" data-ref="${Key}" title="${Label}"><b data-ref="${Key}T">0</b><small data-ref="${Key}P">0</small></div>`).join('')}
            </div>`;
        const R = Refs(Root);
        return () =>
        {
            for (const [Key] of Corners)
            {
                const Temp = Deck.Tyres[Key];
                R.Put(`${Key}T`, String(Math.round(Temp)));
                R.Put(`${Key}P`, (2.1 + (Temp - 30) * 0.004).toFixed(2));
                R[Key].dataset.heat = Temp < 60 ? 'Cold' : Temp < 100 ? 'Ideal' : 'Hot';
            }
        };
    },
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    DECK
//------------------------------------------------------------------------------------------------------------------------

/**
 * @param {{ Car: string, Train: import('../Shared/Powertrain.js').Powertrain, Core: { Update: Function },
 *           Song?: { Title: string, Artist: string, Length: number }, Custom?: Object<string, Function>,
 *           Hmi?: string, Width?: number, Height?: number, Mode?: string, OnMode?: Function }} Options
 */
export function MountDeck({ Car, Train, Core, Song = { Title: 'Night Drive', Artist: 'Static Harbour', Length: 214 }, Custom = {}, Hmi = '', Width = 1920, Height = 720, Mode = 'Cruise', OnMode = () => {} })
{
    const Stage = document.getElementById('Stage');
    const Dash  = document.getElementById('Dash');
    const Find  = (Id) => document.getElementById(Id);
    FitDisplay(Stage, Width, Height + 200, 20);

    const Deck = {
        Mode,
        Song,
        DriveSeconds: 14320,                                        // seeds a 3 h 58 min trip (matches 248.7 km)
        Route: { Step: 0, Left: Route[0].Metres },
        Lap: { Count: 1, Clock: 0, Metres: 0, Last: 0, Best: 118.4 },
        Tyres: { FL: 38, FR: 38, RL: 36, RR: 36 },
    };
    const Echo = LampEcho();
    const Link = OpenLink(Car);

    // Panels.
    const Updaters = [];
    Dash.querySelectorAll('[data-panel]').forEach((Root) =>
    {
        const Make = Custom[Root.dataset.panel] || Panels[Root.dataset.panel];
        if (!Make) return;
        Root.classList.add('Panel', `Panel${Root.dataset.panel}`);
        const Update = Make(Root, Train, Deck);
        const Group  = Root.closest('.Group');
        Updaters.push({ Root, Group, Update: typeof Update === 'function' ? Update : Update.Update });
    });

    // Tell-tales and shift lights.
    const TellRow = Find('Telltales');
    if (TellRow && !TellRow.children.length) TellRow.innerHTML = Telltales.map(([Key, Tone]) => `<i data-telltale="${Key}" class="${Tone}">${LampGlyphs[Key] || ''}</i>`).join('');
    const Lights = Find('ShiftLights');
    const LightCount = 15;
    if (Lights) Lights.innerHTML = Array.from({ length: LightCount }, (_, Index) => `<i class="${Index < 5 ? 'Green' : Index < 10 ? 'Amber' : 'Red'}"></i>`).join('');
    const LightCells = Lights ? [...Lights.children] : [];

    // Steering-wheel strip.
    const Wheel = Find('Wheel');
    Wheel.innerHTML = `
        <div class="WheelGroup"><small>Drive mode · 1–4</small>
            <div class="Pills ModeKeys">${ModeOrder.map((Name) => `<button data-mode-key="${Name}">${Name}</button>`).join('')}<i class="Glide"></i></div></div>
        <div class="WheelGroup"><small>Selector · paddles Q E</small>
            <div class="Row"><button class="Round" data-paddle="-1">−</button>
            <div class="Pills GearKeys">${['P', 'R', 'N', 'D', 'M'].map((Letter) => `<button data-gear-key="${Letter}">${Letter}</button>`).join('')}<i class="Glide"></i></div>
            <button class="Round" data-paddle="1">+</button></div></div>
        <div class="WheelGroup"><small>Pedals · ↓ ↑</small>
            <div class="Row"><button class="Hold Brake" id="BrakeKey"><i></i><span>Brake</span></button><button class="Hold Throttle" id="ThrottleKey"><i></i><span>Throttle</span></button></div></div>
        <div class="WheelGroup"><small>Run</small>
            <div class="Row"><button class="Key" id="DemoKey">Demo drive</button><button class="Key" id="SweepKey">Ignition sweep</button></div></div>
        <div class="WheelGroup LinkGroup"><small>Centre display</small>
            <div class="Row"><span class="LinkDot" id="LinkDot"></span><span id="LinkText">Not open</span>${Hmi ? `<a class="Key" href="${Hmi}" target="_blank" rel="noopener">Open HMI</a>` : ''}</div></div>`;

    const Glide = (Group, Button) =>
    {
        const Bar = Group.querySelector('.Glide');
        if (!Button || !Button.offsetWidth) return;
        Bar.style.width = `${Button.offsetWidth}px`;
        Bar.style.transform = `translateX(${Button.offsetLeft}px)`;
    };
    const ModeKeys = Wheel.querySelector('.ModeKeys'), GearKeys = Wheel.querySelector('.GearKeys');
    ModeKeys.addEventListener('click', (Press) => { const Button = Press.target.closest('[data-mode-key]'); if (Button) SetMode(Button.dataset.modeKey); });
    GearKeys.addEventListener('click', (Press) => { const Button = Press.target.closest('[data-gear-key]'); if (Button) Train.SetSelector(Button.dataset.gearKey); });
    Wheel.querySelectorAll('[data-paddle]').forEach((Button) => Button.addEventListener('click', () => Train.Paddle(+Button.dataset.paddle)));
    BindHold(Find('ThrottleKey'), () => { Train.ThrottleGoal = 1; }, () => { Train.ThrottleGoal = 0; });
    BindHold(Find('BrakeKey'), () => { Train.BrakeGoal = 1; }, () => { Train.BrakeGoal = 0; });
    Find('SweepKey').addEventListener('click', () => Train.Sweep());
    Find('DemoKey').addEventListener('click', () => SetDemo(!Train.Demo));
    BindPowertrainKeys(Train);
    window.addEventListener('keydown', (Press) => { const Index = '1234'.indexOf(Press.key); if (Index >= 0) SetMode(ModeOrder[Index]); });

    function SetDemo(On)
    {
        Train.Demo = On;
        if (On) Train.DemoClock = 0;
        else { Train.ThrottleGoal = 0; Train.BrakeGoal = 0.6; setTimeout(() => { if (!Train.Demo) Train.BrakeGoal = 0; }, 2500); }
        Find('DemoKey').classList.toggle('Running', On);
        Find('DemoKey').textContent = On ? 'Stop demo' : 'Demo drive';
    }

    // Drive mode: data-mode on the dash restyles it; groups cross-fade; a wash sweeps across; the title rolls.
    function SetMode(Name, Quiet = false)
    {
        if (!DriveModes[Name]) return;
        const Changed = Name !== Deck.Mode;
        Deck.Mode = Name;
        Dash.dataset.mode = Name;
        Train.SetStyle(DriveModes[Name].Style);
        Dash.querySelectorAll('.Group').forEach((Group) => Group.classList.toggle('Shown', Group.dataset.modes.split(' ').includes(Name)));
        ModeKeys.querySelectorAll('button').forEach((Button) => Button.classList.toggle('On', Button.dataset.modeKey === Name));
        Glide(ModeKeys, ModeKeys.querySelector('.On'));
        const Title = Find('ModeName');
        if (Title)
        {
            Title.textContent = Name;
            Title.classList.remove('Roll'); void Title.offsetWidth; Title.classList.add('Roll');
        }
        if (Find('ModeLine')) Find('ModeLine').textContent = DriveModes[Name].Line;
        const Wash = Find('ModeWash');
        if (Wash && Changed && !Quiet) { Wash.classList.remove('Go'); void Wash.offsetWidth; Wash.classList.add('Go'); }
        if (Changed) Link.Send('Mode', { Mode: Name });
        OnMode(Name);
    }

    // Lamp state from the centre display.
    Link.On((Kind, Payload) =>
    {
        if (Kind === 'Lamps') Echo.Set(Payload);
        else if (Kind === 'Selector') Train.SetSelector(Payload.Selector);    // the HMI's own selector row
        else if (Kind === 'Hello') Link.Send('Mode', { Mode: Deck.Mode });
    });
    Link.Send('Hello', {});

    let ShownSelector = '', LastSend = 0, LastClock = 0, Wall = performance.now();
    function Frame()
    {
        const Now = performance.now(), Delta = Math.min(0.25, (Now - Wall) / 1000);
        Wall = Now;
        const Before = Train.Odometer;
        Train.Step();
        const Moved = (Train.Odometer - Before) * 1000;
        if (Train.Kmh > 1) Deck.DriveSeconds += Delta;

        // Route and lap progress follow the distance actually driven.
        Deck.Route.Left -= Moved;
        if (Deck.Route.Left <= 0) { Deck.Route.Step = (Deck.Route.Step + 1) % Route.length; Deck.Route.Left = Route[Deck.Route.Step].Metres; }
        if (Train.Kmh > 1) Deck.Lap.Clock += Delta;
        Deck.Lap.Metres += Moved;
        if (Deck.Lap.Metres >= LapLength * 1000)
        {
            Deck.Lap.Metres -= LapLength * 1000;
            Deck.Lap.Last = Deck.Lap.Clock;
            Deck.Lap.Best = Math.min(Deck.Lap.Best, Deck.Lap.Clock);
            Deck.Lap.Clock = 0;
            Deck.Lap.Count++;
        }
        // Tyres warm with speed, cornering (outside wheels) and braking (fronts); cool slowly towards 30 °C.
        const Speed = Train.Kmh, Lat = Train.GLat;
        for (const Key of ['FL', 'FR', 'RL', 'RR'])
        {
            const Outside = (Key.endsWith('L') ? -Lat : Lat);
            const Goal = 30 + Speed * 0.32 + Math.max(0, Outside) * 46 + (Key.startsWith('F') ? Train.Brake * Speed * 0.12 : Train.Throttle * Speed * 0.06);
            Deck.Tyres[Key] += (Goal - Deck.Tyres[Key]) * (1 - Math.exp(-Delta * 0.35));
        }

        Core.Update();
        for (const Each of Updaters) if (!Each.Group || Each.Group.classList.contains('Shown')) Each.Update();

        // Shift lights: fill from 62 % of the redline, flash at the limiter.
        if (LightCells.length)
        {
            const Rpm = Math.max(Train.Rpm, Train.SweepLevel * Train.Redline);
            const Lit = Math.round(Clamp((Rpm - Train.Redline * 0.62) / (Train.Redline * 0.38), 0, 1) * LightCount);
            LightCells.forEach((Cell, Index) => Cell.classList.toggle('Lit', Index < Lit));
            Lights.classList.toggle('Flash', Train.Limiter || Rpm >= Train.Redline);
        }

        // Tell-tales: lamps echo the centre display; park brake and low fuel are the cluster's own.
        Echo.Speed = Speed;
        Echo.Step();
        if (TellRow)
        {
            SyncTelltales(TellRow, Echo);
            TellRow.querySelector('[data-telltale="Park"]')?.classList.toggle('Lit', Train.Selector === 'P');
            TellRow.querySelector('[data-telltale="FuelLow"]')?.classList.toggle('Lit', Train.Fuel < 0.12);
        }

        if (Train.Selector !== ShownSelector)
        {
            ShownSelector = Train.Selector;
            GearKeys.querySelectorAll('button').forEach((Button) => Button.classList.toggle('On', Button.dataset.gearKey === ShownSelector));
            Glide(GearKeys, GearKeys.querySelector('.On'));
        }
        Find('ThrottleKey').querySelector('i').style.width = `${Train.Throttle * 100}%`;
        Find('BrakeKey').querySelector('i').style.width = `${Train.Brake * 100}%`;

        if (Now - LastClock > 1000)
        {
            LastClock = Now;
            if (Find('DashClock')) Find('DashClock').textContent = ClockText();
            if (Find('OdoValue')) Find('OdoValue').textContent = Math.floor(Train.Odometer).toLocaleString('en-GB');
            const Linked = !!Link.Fresh('Lamps', 2500);
            Find('LinkDot').classList.toggle('On', Linked);
            Find('LinkText').textContent = Linked ? `${Car} HMI linked` : 'Not open';
        }
        if (Now - LastSend > 100)
        {
            LastSend = Now;
            Link.Send('Drive', { Kmh: Train.Kmh, Velocity: Train.Velocity, Selector: Train.Selector, Gear: Train.GearLabel, Brake: Train.Brake, Throttle: Train.Throttle, GLong: Train.GLong, GLat: Train.GLat, Fuel: Train.Fuel, Range: Train.Range, Mode: Deck.Mode });
        }
        requestAnimationFrame(Frame);
    }

    SetMode(Mode, true);
    requestAnimationFrame(() => { Glide(ModeKeys, ModeKeys.querySelector('.On')); Train.Sweep(); });
    requestAnimationFrame(Frame);

    const Api = { Deck, Train, Echo, Link, SetMode, SetDemo, get Mode() { return Deck.Mode; } };
    window.ClusterCondition = Api;                                   // 🔍 exposed for the capture script
    return Api;
}

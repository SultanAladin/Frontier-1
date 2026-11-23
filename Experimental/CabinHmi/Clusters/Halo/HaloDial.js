// HALO DIAL AND G COMPASS — CORE OF THE AFTERGLOW DASHBOARD CLUSTER
// 🧩 Dashboard instrument cluster for the Afterglow car after the round-speedo reference (SourceImages/HMI/c12bcefe…jpg): a halo speed
//    dial with a mood label, coral needle riding a glass sweep band, cardinal tick crosses and a boxed figure; flanked
//    by a power / regen arc on the left and a fuel arc (F … E) on the right. The G compass (crosshair, live dot with a
//    fading trail, MAX readout) lives in the sheet.
// 📝 Pure SVG; values come from the shared Powertrain. Decimal commas follow the reference (0,00 G).

const Tau = Math.PI * 2;
const Polar = (Cx, Cy, Radius, Degrees) => { const Angle = (Degrees - 90) * Math.PI / 180; return [Cx + Math.cos(Angle) * Radius, Cy + Math.sin(Angle) * Radius]; };
function ArcPath(Cx, Cy, Radius, From, To)
{
    const [X1, Y1] = Polar(Cx, Cy, Radius, From), [X2, Y2] = Polar(Cx, Cy, Radius, To);
    const Large = Math.abs(To - From) > 180 ? 1 : 0, Sweep = To > From ? 1 : 0;
    return `M${X1.toFixed(2)},${Y1.toFixed(2)}A${Radius},${Radius} 0 ${Large} ${Sweep} ${X2.toFixed(2)},${Y2.toFixed(2)}`;
}
const Comma = (Value, Places) => Value.toFixed(Places).replace('.', ',');

// Speed scale: 0 … 260 km/h over 270°, starting at the bottom-left (225° clockwise from 12 o'clock).
const Start = 225, Span = 270, Top = 260;
const SpeedAngle = (Kmh) => Start + Math.min(1, Kmh / Top) * Span;

export function MountHaloDial(Host, Train, { ModeName = () => 'Cruise', OnSelector = () => {} } = {})
{
    const Cx = 330, Cy = 290;
    const Numbers = [0, 40, 80, 120, 160, 200, 240].map((Kmh) => { const [X, Y] = Polar(Cx, Cy, 146, SpeedAngle(Kmh)); return `<text x="${X.toFixed(1)}" y="${(Y + 5).toFixed(1)}" text-anchor="middle">${Kmh}</text>`; }).join('');
    const Minor = Array.from({ length: 53 }, (_, Index) => { const Kmh = Index * 5, Major = Kmh % 20 === 0; const [X1, Y1] = Polar(Cx, Cy, 170, SpeedAngle(Kmh)), [X2, Y2] = Polar(Cx, Cy, Major ? 160 : 165, SpeedAngle(Kmh)); return `<line x1="${X1.toFixed(1)}" y1="${Y1.toFixed(1)}" x2="${X2.toFixed(1)}" y2="${Y2.toFixed(1)}" class="${Major ? 'Major' : ''}"/>`; }).join('');
    const Cross = [0, 90, 180, 270].map((Degrees) =>
    {
        const [X, Y] = Polar(Cx, Cy, 120, Degrees), [X2, Y2] = Polar(Cx, Cy, 92, Degrees);
        return `<line x1="${X.toFixed(1)}" y1="${Y.toFixed(1)}" x2="${X2.toFixed(1)}" y2="${Y2.toFixed(1)}"/><circle cx="${X2.toFixed(1)}" cy="${Y2.toFixed(1)}" r="2.4"/>`;
    }).join('');

    Host.innerHTML = `
    <svg class="HaloDial" viewBox="0 0 660 580">
        <defs>
            <radialGradient id="HaloFace" cx="0.5" cy="0.45" r="0.6"><stop offset="0" stop-color="#2a1530" stop-opacity="0.75"/><stop offset="1" stop-color="#140a1c" stop-opacity="0.35"/></radialGradient>
            <linearGradient id="HaloBand" x1="0" x2="1" y1="1" y2="0"><stop offset="0" stop-color="#ffffff" stop-opacity="0.12"/><stop offset="1" stop-color="#ffffff" stop-opacity="0.42"/></linearGradient>
            <filter id="HaloBlur" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="4"/></filter>
        </defs>
        <circle cx="${Cx}" cy="${Cy}" r="250" class="Bezel"/>
        <path class="BezelArc" d="${ArcPath(Cx, Cy, 238, 205, 515)}"/>
        <circle cx="${Cx}" cy="${Cy}" r="214" fill="url(#HaloFace)" class="Face"/>
        <path class="Band" d="${ArcPath(Cx, Cy, 196, Start, Start + Span)}"/>
        <path class="BandLive" id="HaloBand" d=""/>
        <circle cx="${Cx}" cy="${Cy}" r="176" class="Inner"/>
        <g class="Ticks">${Minor}</g>
        <g class="Numbers">${Numbers}</g>
        <g class="Cross">${Cross}</g>
        <g class="Box">
            <path d="M${Cx - 76},${Cy - 92}h-12v22M${Cx + 76},${Cy - 92}h12v22M${Cx - 76},${Cy + 74}h-12v-22M${Cx + 76},${Cy + 74}h12v-22"/>
            <line x1="${Cx - 52}" y1="${Cy - 92}" x2="${Cx + 52}" y2="${Cy - 92}"/><line x1="${Cx - 52}" y1="${Cy + 74}" x2="${Cx + 52}" y2="${Cy + 74}"/>
        </g>
        <circle class="MoodDot" cx="${Cx}" cy="${Cy - 236}" r="4"/>
        <text class="MoodLabel" id="HaloMood" x="${Cx}" y="${Cy - 196}" text-anchor="middle">CALM</text>
        <text class="SpeedFigure" id="HaloSpeed" x="${Cx}" y="${Cy + 36}" text-anchor="middle">0</text>
        <text class="SpeedUnit" x="${Cx}" y="${Cy + 62}" text-anchor="middle">km/h</text>
        <g class="Needle" id="HaloNeedle"><line class="NeedleGlow" filter="url(#HaloBlur)"/><line class="NeedleLine"/></g>
        <g class="Selector" id="HaloSelector" transform="translate(${Cx} ${Cy + 150})">
            <text class="Side" id="HaloPrev" x="-34" y="0" text-anchor="middle">R</text>
            <text class="Now" id="HaloGear" x="0" y="4" text-anchor="middle">P</text>
            <text class="Side" id="HaloNext" x="34" y="0" text-anchor="middle">N</text>
        </g>

        <g class="SideArc Power">
            <path class="Track" d="${ArcPath(Cx, Cy, 272, 238, 302)}"/>
            <path class="Live" id="PowerArc" d=""/>
            <path class="Live Regen" id="RegenArc" d=""/>
            <text x="${Polar(Cx, Cy, 296, 238)[0]}" y="${Polar(Cx, Cy, 296, 238)[1] + 4}" text-anchor="middle">−</text>
            <text x="${Polar(Cx, Cy, 296, 302)[0]}" y="${Polar(Cx, Cy, 296, 302)[1] + 4}" text-anchor="middle">+</text>
            <text class="Caption" x="${Polar(Cx, Cy, 300, 270)[0] - 6}" y="${Cy + 4}" text-anchor="middle" transform="rotate(-90 ${Polar(Cx, Cy, 300, 270)[0] - 6} ${Cy})">POWER</text>
        </g>
        <g class="SideArc Fuel">
            <path class="Track" d="${ArcPath(Cx, Cy, 272, 58, 122)}"/>
            <path class="Live" id="FuelArc" d=""/>
            <path class="Hook" d="M${Polar(Cx, Cy, 272, 122)[0]},${Polar(Cx, Cy, 272, 122)[1]}l-12,0"/>
            <text x="${Polar(Cx, Cy, 290, 58)[0] + 4}" y="${Polar(Cx, Cy, 290, 58)[1]}" class="Letter">F</text>
            <text x="${Polar(Cx, Cy, 290, 122)[0] + 4}" y="${Polar(Cx, Cy, 290, 122)[1] + 10}" class="Letter">E</text>
            <g transform="translate(${Polar(Cx, Cy, 306, 90)[0] - 2} ${Cy - 14})" class="PlugGlyph"><path d="M3 21V5a2 2 0 0 1 2-2h7a2 2 0 0 1 2 2v16M2 21h13M5.5 7h6M14 9h2.5a1.5 1.5 0 0 1 1.5 1.5V17a1.5 1.5 0 0 0 3 0V8l-3-3"/></g>
            <text class="Percent" id="FuelText" x="${Polar(Cx, Cy, 306, 90)[0] + 8}" y="${Cy + 34}" text-anchor="middle">62%</text>
        </g>
    </svg>`;

    const Find = (Id) => Host.querySelector(`#${Id}`);
    const Shown = {};
    const Put = (Key, Id, Text) => { if (Shown[Key] !== Text) { Shown[Key] = Text; Find(Id).textContent = Text; } };
    let Needle = 0, LastShift = Train.ShiftCount;
    const Letters = ['P', 'R', 'N', 'D'];
    Find('HaloSelector').addEventListener('click', (Press) =>
    {
        const Hit = Press.target.closest('text');
        if (!Hit) return;
        const Index = Letters.indexOf(Train.Selector === 'M' ? 'D' : Train.Selector);
        const Next = Hit.id === 'HaloPrev' ? Letters[Math.max(0, Index - 1)] : Hit.id === 'HaloNext' ? Letters[Math.min(3, Index + 1)] : null;
        if (Next) { Train.SetSelector(Next); OnSelector(Next); }
    });

    function Update()
    {
        const Sweep = Train.SweepLevel;
        const Goal  = Math.max(Train.Kmh, Sweep * Top);
        Needle += (Goal - Needle) * 0.35;
        const Angle = SpeedAngle(Needle);
        Find('HaloBand').setAttribute('d', ArcPath(Cx, Cy, 196, Start, Math.max(Start + 0.5, Angle)));
        const [X1, Y1] = Polar(Cx, Cy, 168, Angle), [X2, Y2] = Polar(Cx, Cy, 226, Angle);
        Find('HaloNeedle').querySelectorAll('line').forEach((Line) => { Line.setAttribute('x1', X1); Line.setAttribute('y1', Y1); Line.setAttribute('x2', X2); Line.setAttribute('y2', Y2); });
        Put('Speed', 'HaloSpeed', String(Math.round(Needle)));
        Put('Mood', 'HaloMood', ModeName().toUpperCase());

        const Selector = Train.Selector === 'M' ? 'D' : Train.Selector;
        const Index = Letters.indexOf(Selector);
        Put('Gear', 'HaloGear', Selector);
        Put('Prev', 'HaloPrev', Letters[Index - 1] || '');
        Put('Next', 'HaloNext', Letters[Index + 1] || '');
        if (Train.ShiftCount !== LastShift)
        {
            LastShift = Train.ShiftCount;
            const Now = Find('HaloGear');
            Now.classList.remove('Pop'); void Now.getBoundingClientRect(); Now.classList.add('Pop');
        }

        // Power (+) from throttle, regen (−) from braking; both share the left arc, meeting at its middle.
        const Power = Math.max(Sweep, Train.Throttle * Math.min(1, 0.35 + Train.Kmh / 120));
        const Regen = Sweep > 0 ? 0 : Math.min(1, Train.Brake * Math.min(1, Train.Kmh / 30));
        Find('PowerArc').setAttribute('d', Power > 0.01 ? ArcPath(Cx, Cy, 272, 270, 270 + Power * 32) : '');
        Find('RegenArc').setAttribute('d', Regen > 0.01 ? ArcPath(Cx, Cy, 272, 270 - Regen * 32, 270) : '');
        const Fuel = Math.max(Train.Fuel, Sweep);
        Find('FuelArc').setAttribute('d', ArcPath(Cx, Cy, 272, 122 - Fuel * 64, 122));
        Find('FuelArc').classList.toggle('Low', Train.Fuel < 0.12);
        Put('Fuel', 'FuelText', `${Math.round(Train.Litres)} L`);
    }
    return { Update };
}

export function MountGCompass(Host, Train)
{
    const Size = 300, Mid = Size / 2, Scale = 110;               // 1 g at 110 px
    Host.innerHTML = `
    <div class="GCompassHead"><b>G METER</b><button class="GReset" id="GReset">Reset</button></div>
    <svg class="GCompass" viewBox="0 0 ${Size} ${Size}">
        <circle cx="${Mid}" cy="${Mid}" r="${Scale}" class="Ring"/>
        <circle cx="${Mid}" cy="${Mid}" r="${Scale * 0.5}" class="Ring Inner"/>
        <circle cx="${Mid}" cy="${Mid}" r="${Scale * 0.25}" class="Ring Core"/>
        <line x1="${Mid}" y1="20" x2="${Mid}" y2="${Size - 20}" class="Axis"/>
        <line x1="20" y1="${Mid}" x2="${Size - 20}" y2="${Mid}" class="Axis"/>
        <polyline id="GTrail" class="Trail" points=""/>
        <circle id="GPeak" class="Peak" r="3"/>
        <g id="GDot"><circle r="14" class="DotHalo"/><circle r="9" class="Dot"/></g>
    </svg>
    <span class="GLabel Top" id="GTop">0,0</span><span class="GLabel Bottom" id="GBottom">0,0</span>
    <span class="GLabel Left" id="GLeft">0,0</span><span class="GLabel Right" id="GRight">0,0</span>
    <div class="GMax"><em>MAX</em><b id="GMax">0,00 G</b></div>`;
    const Find = (Id) => Host.querySelector(`#${Id}`);
    const Trail = [];
    let Peak = [Mid, Mid], LastSample = 0;
    Find('GReset').addEventListener('click', () => { Train.GMax = 0; Peak = [Mid, Mid]; Trail.length = 0; });
    const Shown = {};
    const Put = (Id, Text) => { if (Shown[Id] !== Text) { Shown[Id] = Text; Find(Id).textContent = Text; } };

    function Update()
    {
        // Screen: braking pushes the dot up (forward), acceleration down; lateral to the outside of the bend.
        const X = Mid + Math.max(-1.2, Math.min(1.2, Train.GLat)) * Scale;
        const Y = Mid + Math.max(-1.2, Math.min(1.2, Train.GLong)) * Scale;
        Find('GDot').setAttribute('transform', `translate(${X.toFixed(1)} ${Y.toFixed(1)})`);
        const Now = performance.now();
        if (Now - LastSample > 90) { LastSample = Now; Trail.push([X, Y]); if (Trail.length > 28) Trail.shift(); Find('GTrail').setAttribute('points', Trail.map((P) => P.map((V) => V.toFixed(1)).join(',')).join(' ')); }
        if (Math.hypot(X - Mid, Y - Mid) >= Math.hypot(Peak[0] - Mid, Peak[1] - Mid)) Peak = [X, Y];
        Find('GPeak').setAttribute('cx', Peak[0]); Find('GPeak').setAttribute('cy', Peak[1]);
        Put('GTop', Comma(Math.max(0, -Train.GLong), 1));
        Put('GBottom', Comma(Math.max(0, Train.GLong), 1));
        Put('GLeft', Comma(Math.max(0, -Train.GLat), 1));
        Put('GRight', Comma(Math.max(0, Train.GLat), 1));
        Put('GMax', `${Comma(Train.GMax, 2)} G`);
    }
    return { Update };
}

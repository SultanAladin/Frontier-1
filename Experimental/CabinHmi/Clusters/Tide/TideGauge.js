// TIDE GAUGE — CORE OF THE NOCTURNE DASHBOARD CLUSTER
// 🧩 Dashboard instrument cluster for the Nocturne car after the round RPM / turbo reference (SourceImages/HMI/64a13b8d…jpg): engine
//    speed scale on the left and boost scale on the right (white rails, live red inner arcs), eco leaf and battery
//    segment fans at the top corners, a red-ticked speed ring with a dotted orbit and a glass sweep band, a thin
//    central figure and a P R N D row. Moon-white rails, signal red, eco green and charge blue on lake black.
// 📝 Pure SVG; readings from the shared Powertrain (hybrid turbo set-up).

const Polar = (Cx, Cy, Radius, Degrees) => { const Angle = (Degrees - 90) * Math.PI / 180; return [Cx + Math.cos(Angle) * Radius, Cy + Math.sin(Angle) * Radius]; };
function ArcPath(Cx, Cy, Radius, From, To)
{
    if (Math.abs(To - From) < 0.01) return '';
    const [X1, Y1] = Polar(Cx, Cy, Radius, From), [X2, Y2] = Polar(Cx, Cy, Radius, To);
    const Large = Math.abs(To - From) > 180 ? 1 : 0, Sweep = To > From ? 1 : 0;
    return `M${X1.toFixed(2)},${Y1.toFixed(2)}A${Radius},${Radius} 0 ${Large} ${Sweep} ${X2.toFixed(2)},${Y2.toFixed(2)}`;
}
const Fixed = (Value) => Value.toFixed(1);

const Cx = 500, Cy = 330;
const SpeedStart = 225, SpeedSpan = 270, SpeedTop = 300;
const SpeedAngle = (Kmh) => SpeedStart + Math.min(1, Kmh / SpeedTop) * SpeedSpan;
// Side rails: left (engine) runs 205° → 325° bottom-up; right (boost) runs 155° → 35° bottom-up.
const RpmAngle   = (Rpm) => 205 + Math.min(1, Rpm / 7000) * 120;
const BoostAngle = (Bar) => 155 - Math.min(1, Bar / 3) * 120;

export function MountTideGauge(Host, Train)
{
    const RailR = 318, LiveR = 304;
    const RpmTicks = Array.from({ length: 8 }, (_, K) =>
    {
        const A = RpmAngle(K * 1000), [X1, Y1] = Polar(Cx, Cy, RailR, A), [X2, Y2] = Polar(Cx, Cy, RailR + 12, A), [Lx, Ly] = Polar(Cx, Cy, RailR + 32, A);
        return `<line x1="${Fixed(X1)}" y1="${Fixed(Y1)}" x2="${Fixed(X2)}" y2="${Fixed(Y2)}"/><text x="${Fixed(Lx)}" y="${Fixed(Ly + 6)}" text-anchor="middle" data-rpm="${K}">${K}</text>`;
    }).join('');
    const BoostTicks = Array.from({ length: 4 }, (_, K) =>
    {
        const A = BoostAngle(K), [X1, Y1] = Polar(Cx, Cy, RailR, A), [X2, Y2] = Polar(Cx, Cy, RailR - 12, A), [Lx, Ly] = Polar(Cx, Cy, RailR - 32, A);
        return `<line x1="${Fixed(X1)}" y1="${Fixed(Y1)}" x2="${Fixed(X2)}" y2="${Fixed(Y2)}"/><text x="${Fixed(Lx)}" y="${Fixed(Ly + 6)}" text-anchor="middle">${K}</text>`;
    }).join('');
    const SpeedTicks = [0, 50, 100, 150, 200, 250, 300].map((Kmh) =>
    {
        const A = SpeedAngle(Kmh), [X1, Y1] = Polar(Cx, Cy, 196, A), [X2, Y2] = Polar(Cx, Cy, 222, A), [Lx, Ly] = Polar(Cx, Cy, 246, A);
        return `<line class="Tick" x1="${Fixed(X1)}" y1="${Fixed(Y1)}" x2="${Fixed(X2)}" y2="${Fixed(Y2)}" data-kmh="${Kmh}"/><text x="${Fixed(Lx)}" y="${Fixed(Ly + 6)}" text-anchor="middle">${Kmh}</text>`;
    }).join('');
    const Fan = (Id, From, To) => Array.from({ length: 9 }, (_, Index) =>
    {
        const A = From + (To - From) * (Index / 9), B = From + (To - From) * ((Index + 0.82) / 9);
        const [X1, Y1] = Polar(Cx, Cy, 372, A), [X2, Y2] = Polar(Cx, Cy, 372, B), [X3, Y3] = Polar(Cx, Cy, 398, B), [X4, Y4] = Polar(Cx, Cy, 398, A);
        return `<path data-seg="${Index}" d="M${Fixed(X1)},${Fixed(Y1)}L${Fixed(X2)},${Fixed(Y2)}L${Fixed(X3)},${Fixed(Y3)}L${Fixed(X4)},${Fixed(Y4)}Z"/>`;
    }).join('');

    Host.innerHTML = `
    <svg class="TideGauge" viewBox="0 0 1000 640">
        <defs>
            <filter id="TideGlow" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="3"/></filter>
            <radialGradient id="TideFace" cx="0.5" cy="0.5" r="0.5"><stop offset="0.55" stop-color="#050913" stop-opacity="0.82"/><stop offset="1" stop-color="#050913" stop-opacity="0"/></radialGradient>
        </defs>
        <circle cx="${Cx}" cy="${Cy}" r="440" fill="url(#TideFace)"/>
        <path class="Bezel Glow" d="${ArcPath(Cx, Cy, 452, 222, 318)}" filter="url(#TideGlow)"/><path class="Bezel" d="${ArcPath(Cx, Cy, 452, 222, 318)}"/>
        <path class="Bezel Glow" d="${ArcPath(Cx, Cy, 452, 42, 138)}" filter="url(#TideGlow)"/><path class="Bezel" d="${ArcPath(Cx, Cy, 452, 42, 138)}"/>
        <path class="BezelBand" d="${ArcPath(Cx, Cy, 432, 228, 312)}"/><path class="BezelBand" d="${ArcPath(Cx, Cy, 432, 48, 132)}"/>

        <g class="Eco" id="TideEco">${Fan('Eco', 300, 330)}</g>
        <g class="Charge" id="TideCharge">${Fan('Charge', 60, 30)}</g>
        <g class="EcoTag" transform="translate(${Fixed(Polar(Cx, Cy, 352, 334)[0])} ${Fixed(Polar(Cx, Cy, 352, 334)[1])})">
            <path class="Leaf" d="M2 26C2 12 12 2 30 2c0 18-10 28-24 28M2 26 18 12M10 22V14M14 18h6" transform="translate(14 -2) scale(0.6)"/>
            <text x="-26" y="16" id="EcoText">80%</text>
        </g>
        <g class="ChargeTag" transform="translate(${Fixed(Polar(Cx, Cy, 352, 26)[0])} ${Fixed(Polar(Cx, Cy, 352, 26)[1])})">
            <path class="Bolt" d="M18 2 6 20h10l-3 16 15-22H18Z" transform="translate(-6 -1) scale(0.5)"/>
            <text x="14" y="16" id="ChargeText">62%</text>
        </g>

        <g class="Rail Rpm">
            <path class="RailLine" d="${ArcPath(Cx, Cy, RailR, 205, 325)}"/>
            <path class="RedZone" d="${ArcPath(Cx, Cy, RailR + 4, RpmAngle(Train.Redline), 325)}"/>
            <path class="Live" id="RpmLive" d=""/>
            <g class="Ticks">${RpmTicks}</g>
            <text class="RailCaption" x="${Fixed(Polar(Cx, Cy, RailR, 205)[0] - 30)}" y="${Fixed(Polar(Cx, Cy, RailR, 205)[1] + 52)}">RPM <tspan>×1000</tspan></text>
        </g>
        <g class="Rail Boost">
            <path class="RailLine" d="${ArcPath(Cx, Cy, RailR, 35, 155)}"/>
            <path class="Live" id="BoostLive" d=""/>
            <g class="Ticks">${BoostTicks}</g>
            <text class="RailCaption" x="${Fixed(Polar(Cx, Cy, RailR, 155)[0] - 40)}" y="${Fixed(Polar(Cx, Cy, RailR, 155)[1] + 52)}">TURBO <tspan>bar</tspan></text>
        </g>

        <path class="SweepBand" id="SpeedBand" d=""/>
        <circle class="Orbit" cx="${Cx}" cy="${Cy}" r="186"/>
        <g class="SpeedTicks">${SpeedTicks}</g>
        <line class="Dash" x1="${Cx - 130}" y1="${Cy - 26}" x2="${Cx - 70}" y2="${Cy - 26}"/><line class="Dash" x1="${Cx + 70}" y1="${Cy - 26}" x2="${Cx + 130}" y2="${Cy - 26}"/>
        <text class="Unit" x="${Cx - 100}" y="${Cy - 2}" text-anchor="middle">km/h</text>
        <text class="GearSmall" id="TideGearSmall" x="${Cx + 100}" y="${Cy - 2}" text-anchor="middle">P</text>
        <text class="SpeedFigure" id="TideSpeed" x="${Cx}" y="${Cy + 22}" text-anchor="middle">0</text>
        <line class="Dash Long" x1="${Cx - 110}" y1="${Cy + 96}" x2="${Cx + 110}" y2="${Cy + 96}"/>
        <g class="Selector" id="TideSelector">${['P', 'R', 'N', 'D'].map((Letter, Index) => `<text x="${Cx - 66 + Index * 44}" y="${Cy + 134}" text-anchor="middle" data-sel="${Letter}">${Letter}</text>`).join('')}</g>
        <line class="Pin" x1="${Cx}" y1="${Cy + 160}" x2="${Cx}" y2="${Cy + 196}"/>
        <g class="Needle" id="TideNeedle"><line class="NeedleGlow" filter="url(#TideGlow)"/><line class="NeedleLine"/></g>
    </svg>`;

    const Find = (Id) => Host.querySelector(`#${Id}`);
    const Shown = {};
    const Put = (Key, Id, Text) => { if (Shown[Key] !== Text) { Shown[Key] = Text; Find(Id).textContent = Text; } };
    let Speed = 0, Rpm = 0, Boost = 0, LastShift = Train.ShiftCount;
    Find('TideSelector').addEventListener('click', (Press) => { const Letter = Press.target.closest('[data-sel]'); if (Letter) Train.SetSelector(Letter.dataset.sel); });

    function Update()
    {
        const Sweep = Train.SweepLevel;
        Speed += (Math.max(Train.Kmh, Sweep * SpeedTop) - Speed) * 0.35;
        Rpm   += (Math.max(Train.Rpm, Sweep * 7000) - Rpm) * 0.5;
        Boost += (Math.max(Train.Boost, Sweep * 3) - Boost) * 0.4;

        Find('RpmLive').setAttribute('d', ArcPath(Cx, Cy, LiveR, 205, RpmAngle(Rpm)));
        Find('BoostLive').setAttribute('d', ArcPath(Cx, Cy, LiveR, BoostAngle(Boost), 155));
        Find('SpeedBand').setAttribute('d', ArcPath(Cx, Cy, 268, SpeedStart, SpeedAngle(Speed)));
        const A = SpeedAngle(Speed), [X1, Y1] = Polar(Cx, Cy, 252, A), [X2, Y2] = Polar(Cx, Cy, 286, A);
        Find('TideNeedle').querySelectorAll('line').forEach((Line) => { Line.setAttribute('x1', X1); Line.setAttribute('y1', Y1); Line.setAttribute('x2', X2); Line.setAttribute('y2', Y2); });
        Host.querySelectorAll('.SpeedTicks .Tick').forEach((Tick) => Tick.classList.toggle('Passed', Speed >= +Tick.dataset.kmh));
        Host.querySelectorAll('[data-rpm]').forEach((Label) => Label.classList.toggle('Passed', Rpm >= +Label.dataset.rpm * 1000));
        Host.classList.toggle('Redline', Rpm >= Train.Redline && Sweep === 0);

        Put('Speed', 'TideSpeed', String(Math.round(Speed)));
        const Gear = Train.Selector === 'D' || Train.Selector === 'M' ? `${Train.Selector}${Train.Gear}` : Train.Selector;
        Put('Gear', 'TideGearSmall', Gear);
        const Letter = Train.Selector === 'M' ? 'D' : Train.Selector;
        Host.querySelectorAll('[data-sel]').forEach((Each) => Each.classList.toggle('On', Each.dataset.sel === Letter));
        if (Train.ShiftCount !== LastShift)
        {
            LastShift = Train.ShiftCount;
            const Small = Find('TideGearSmall');
            Small.classList.remove('Pop'); void Small.getBoundingClientRect(); Small.classList.add('Pop');
        }

        const Eco = Math.max(Sweep, Train.Eco), Charge = Math.max(Sweep, Train.Charge);
        Host.querySelectorAll('#TideEco [data-seg]').forEach((Seg) => Seg.classList.toggle('Lit', Eco > (+Seg.dataset.seg + 0.5) / 9));
        Host.querySelectorAll('#TideCharge [data-seg]').forEach((Seg) => Seg.classList.toggle('Lit', Charge > (+Seg.dataset.seg + 0.5) / 9));
        Put('Eco', 'EcoText', `${Math.round(Train.Eco * 100)}%`);
        Put('Charge', 'ChargeText', `${Math.round(Train.Charge * 100)}%`);
    }
    return { Update };
}

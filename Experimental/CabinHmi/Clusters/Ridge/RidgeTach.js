// RIDGE TACH — CORE OF THE CONTOUR DASHBOARD CLUSTER
// 🧩 Dashboard instrument cluster for the Contour car after the bar-tach reference (SourceImages/HMI/e64b86da…jpg): a rising ribbon tach
//    with dotted fill and a bright leading bracket, engine speed box that floods at the redline, fuel range and
//    coolant meters, drive-style badge, a huge gear figure, road speed and trip. Palette: amethyst fill, verdant data.
// 📝 Pure SVG, rebuilt numbers only when they change. Every reading comes from the shared Powertrain.

const Ns = 'http://www.w3.org/2000/svg';
const Width = 1200, Height = 400;
const RpmTop = 8000;                                               // ribbon end

// Ribbon edges: steep rise on the left that rolls over into a flat run (shape of the reference).
const Upper = (T) => { const X = 40 + T * 950;  const S = Math.min(1, (X - 40) / 380);  return [X, 46 + 258 * Math.pow(1 - S, 2.1)]; };
const Lower = (T) => { const X = 104 + T * 886; const S = Math.min(1, (X - 104) / 346); return [X, 132 + 216 * Math.pow(1 - S, 2.1)]; };

function RibbonPath(From, To, Steps = 64)
{
    const Top = [], Bottom = [];
    for (let Index = 0; Index <= Steps; Index++)
    {
        const T = From + (To - From) * (Index / Steps);
        Top.push(Upper(T));
        Bottom.push(Lower(T));
    }
    return `M${Top.map((P) => P.map((V) => V.toFixed(1)).join(',')).join('L')}L${Bottom.reverse().map((P) => P.map((V) => V.toFixed(1)).join(',')).join('L')}Z`;
}

const EdgePath = (Edge) => { let D = ''; for (let Index = 0; Index <= 80; Index++) { const [X, Y] = Edge(Index / 80); D += `${Index ? 'L' : 'M'}${X.toFixed(1)},${Y.toFixed(1)}`; } return D; };

const FuelGlyph = '<path d="M3 21V5a2 2 0 0 1 2-2h7a2 2 0 0 1 2 2v16M2 21h13M5.5 7h6M14 9h2.5a1.5 1.5 0 0 1 1.5 1.5V17a1.5 1.5 0 0 0 3 0V8l-3-3"/>';
const CoolantGlyph = '<path d="M12 3v11M12 3h4M12 6.5h3M12 10h4"/><circle cx="12" cy="16.5" r="2.5"/><path d="M3 20c1.5-1 3-1 4.5 0s3 1 4.5 0 3-1 4.5 0 3 1 4.5 0"/>';

export function MountRidgeTach(Host, Train, { Modes = ['Cruise', 'Eco', 'Sport', 'Race'], ModeName = () => 'Sport', OnBadge = () => {} } = {})
{
    const Thousands = Array.from({ length: 8 }, (_, Index) => Index + 1);
    const Dividers  = Thousands.slice(0, 7).map((K) => { const [X1, Y1] = Upper(K * 1000 / RpmTop), [X2, Y2] = Lower(K * 1000 / RpmTop); return `<line x1="${X1}" y1="${Y1}" x2="${X2}" y2="${Y2}"/>`; }).join('');
    const Labels    = Thousands.slice(0, 7).map((K) => { const [X, Y] = Lower(K * 1000 / RpmTop); return `<text x="${X - (K < 4 ? 10 : 0)}" y="${Y + 30}" class="${K >= 7 ? 'Red' : ''}" data-k="${K}">${K}</text>`; }).join('');
    const RedZone   = RibbonPath(Train.Redline / RpmTop, 1, 8);

    Host.innerHTML = `
    <svg class="RidgeTach" viewBox="0 0 ${Width} ${Height}">
        <defs>
            <pattern id="RidgeDots" width="5" height="5" patternUnits="userSpaceOnUse"><circle cx="2.5" cy="2.5" r="1.05" fill="#b98cff"/></pattern>
            <linearGradient id="RidgeFade" x1="0" x2="1"><stop offset="0" stop-color="#a970ff" stop-opacity="0"/><stop offset="0.35" stop-color="#a970ff" stop-opacity="0.55"/><stop offset="1" stop-color="#a970ff" stop-opacity="0.9"/></linearGradient>
            <linearGradient id="RidgeEdge" x1="0" x2="1"><stop offset="0" stop-color="#a970ff" stop-opacity="0.05"/><stop offset="0.4" stop-color="#a970ff" stop-opacity="0.9"/></linearGradient>
            <radialGradient id="RidgeFloor" cx="0.5" cy="1" r="0.6"><stop offset="0" stop-color="#7a3df0" stop-opacity="0.35"/><stop offset="1" stop-color="#7a3df0" stop-opacity="0"/></radialGradient>
            <pattern id="FloorDots" width="7" height="7" patternUnits="userSpaceOnUse"><circle cx="3.5" cy="3.5" r="1" fill="#a970ff" fill-opacity="0.35"/></pattern>
            <mask id="FloorMask"><rect x="300" y="270" width="600" height="130" fill="url(#RidgeFloor)"/></mask>
            <filter id="RidgeGlow" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="5"/></filter>
        </defs>
        <rect x="300" y="300" width="600" height="100" fill="url(#FloorDots)" mask="url(#FloorMask)"/>
        <rect x="300" y="300" width="600" height="100" fill="url(#RidgeFloor)" opacity="0.6"/>

        <g class="Ribbon">
            <path class="RedZone" d="${RedZone}"/>
            <path class="Slots" d="${Thousands.slice(3, 7).map((K) => { const A = Upper(K * 1000 / RpmTop), B = Upper((K + 1) * 1000 / RpmTop); return `M${A[0] + 4},${A[1] + 3}H${B[0] - 4}V${Lower(K / 8)[1] - 3}H${A[0] + 4}Z`; }).join('')}"/>
            <path class="Fill" id="RidgeFill"/>
            <path class="Edge" d="${EdgePath(Upper)}"/>
            <path class="Edge" d="${EdgePath(Lower)}"/>
            <g class="Dividers">${Dividers}</g>
            <g class="Leading" id="RidgeLead"><path class="LeadGlow" filter="url(#RidgeGlow)"/><path class="LeadLine"/></g>
            <g class="Labels">${Labels}</g>
        </g>

        <g class="RpmBox" id="RpmBox" transform="translate(1004 40)">
            <rect width="156" height="92" rx="2"/>
            <rect class="Hatch" width="156" height="92" rx="2" fill="url(#RidgeDots)"/>
            <text class="RpmValue" x="146" y="48" text-anchor="end" id="RpmValue">0</text>
            <text class="RpmUnit" x="146" y="78" text-anchor="end">RPM</text>
        </g>

        <g class="Meters" transform="translate(40 22)">
            <g transform="translate(0 0)"><svg class="MeterGlyph" width="26" height="26" viewBox="0 0 24 24">${FuelGlyph}</svg>
                <text class="MeterValue" x="200" y="24" text-anchor="end"><tspan id="FuelValue">0</tspan><tspan class="Unit" dx="3">km</tspan></text>
                <g class="Segments" id="FuelBar">${[0, 1, 2, 3].map((Index) => `<rect x="${Index * 50}" y="34" width="46" height="5" rx="1"/>`).join('')}</g>
                <rect class="Mark Red" x="0" y="42" width="22" height="4"/></g>
            <g transform="translate(0 64)"><svg class="MeterGlyph" width="26" height="26" viewBox="0 0 24 24">${CoolantGlyph}</svg>
                <text class="MeterValue" x="200" y="24" text-anchor="end"><tspan id="CoolantValue">0</tspan><tspan class="Unit" dx="1">°C</tspan></text>
                <g class="Segments" id="CoolantBar">${[0, 1, 2, 3].map((Index) => `<rect x="${Index * 50}" y="34" width="46" height="5" rx="1"/>`).join('')}</g>
                <rect class="Mark Cool" x="0" y="42" width="22" height="4"/><rect class="Mark Red" x="178" y="42" width="22" height="4"/></g>
        </g>

        <g class="StyleBadge" id="StyleBadge" transform="translate(330 268)">
            <path class="Orbit" d="M-6,-52 A44,56 0 0 0 -6,52" />
            <text class="StyleSide" id="StyleUp" x="6" y="-40">C</text>
            <text class="StyleSide" id="StyleDown" x="6" y="52">T</text>
            <path class="Hex" d="M-26,-17 L-14,-26 H14 L26,-17 V17 L14,26 H-14 L-26,17Z"/>
            <text class="StyleLetter" id="StyleLetter" y="11" text-anchor="middle">S</text>
        </g>
        <line class="Leader" x1="374" y1="268" x2="508" y2="268"/><circle class="LeaderEnd" cx="374" cy="268" r="3"/>
        <line class="Leader" x1="700" y1="268" x2="850" y2="268"/><circle class="LeaderEnd" cx="850" cy="268" r="3"/>

        <g class="GearBlock" id="GearBlock" transform="translate(604 270)">
            <text class="GearGlow" id="GearGlow" y="74" text-anchor="middle">P</text>
            <text class="GearFigure" id="GearFigure" y="74" text-anchor="middle">P</text>
            <text class="GearCaption" y="122" text-anchor="middle">GEAR</text>
        </g>

        <g class="SpeedBlock" transform="translate(1160 336)">
            <text class="SpeedFigure" id="SpeedFigure" text-anchor="end">0</text>
            <text class="SpeedUnit" y="26" text-anchor="end">KM/H</text>
            <text class="Trip" y="62" text-anchor="end"><tspan id="TripValue">248.7</tspan><tspan class="Unit" dx="3">km</tspan></text>
        </g>

        <g class="Selector" id="SelectorRow" transform="translate(40 384)">${['P', 'R', 'N', 'D', 'M'].map((Letter, Index) => `<text x="${Index * 36}" data-sel="${Letter}">${Letter}</text>`).join('')}</g>
        <g class="LimiterFlash" id="LimiterFlash"><rect x="1004" y="40" width="156" height="92" rx="2"/></g>
    </svg>`;

    const Find = (Id) => Host.querySelector(`#${Id}`);
    const Fill = Find('RidgeFill'), Lead = Find('RidgeLead'), RpmBox = Find('RpmBox');
    const Shown = {};
    const Put = (Key, Element, Text) => { if (Shown[Key] !== Text) { Shown[Key] = Text; Element.textContent = Text; } };
    let LastShift = Train.ShiftCount, ShownTach = 0;

    Find('StyleBadge').addEventListener('click', () => OnBadge());
    Find('SelectorRow').addEventListener('click', (Press) => { const Letter = Press.target.closest('[data-sel]'); if (Letter) Train.SetSelector(Letter.dataset.sel); });

    function Update()
    {
        const Sweep = Train.SweepLevel;
        const Rpm   = Math.max(Train.Rpm, Sweep * RpmTop * 0.98);
        ShownTach  += (Rpm - ShownTach) * 0.5;                    // one more smoothing stage, so the bracket glides
        const T     = Math.min(1, ShownTach / RpmTop);
        Fill.setAttribute('d', RibbonPath(0, Math.max(0.002, T), Math.max(4, Math.round(T * 64))));
        const [Ux, Uy] = Upper(T), [Lx, Ly] = Lower(T);
        const Bracket = `M${Ux - 12},${Uy}H${Ux}V${Ly}H${Lx - 12}`;
        Lead.firstChild.setAttribute('d', Bracket);
        Lead.lastChild.setAttribute('d', Bracket);
        const Hot = ShownTach >= Train.Redline;
        Host.classList.toggle('Hot', Hot);
        Host.classList.toggle('Limiter', Train.Limiter && Sweep === 0);
        Host.querySelectorAll('.Labels text').forEach((Label) => Label.classList.toggle('Passed', ShownTach >= +Label.dataset.k * 1000));

        Put('Rpm', Find('RpmValue'), String(Math.round(ShownTach / 50) * 50));
        Put('Speed', Find('SpeedFigure'), String(Math.round(Math.max(Train.Kmh, Sweep * 320))));
        Put('Trip', Find('TripValue'), Train.Trip.toFixed(1));
        const Range = Math.round(Train.Range);
        Put('Fuel', Find('FuelValue'), String(Range));
        Put('Coolant', Find('CoolantValue'), String(Math.round(Train.Coolant)));
        Find('FuelBar').querySelectorAll('rect').forEach((Bar, Index) => Bar.classList.toggle('Lit', Train.Fuel > Index * 0.25 + 0.05));
        const Warm = (Train.Coolant - 40) / 80;
        Find('CoolantBar').querySelectorAll('rect').forEach((Bar, Index) => Bar.classList.toggle('Lit', Warm > Index * 0.25));

        const Gear = Train.GearLabel;
        Put('Gear', Find('GearFigure'), Gear);
        Put('GearGlow', Find('GearGlow'), Gear);
        if (Train.ShiftCount !== LastShift)
        {
            LastShift = Train.ShiftCount;
            const Block = Find('GearBlock');
            Block.classList.remove('Pop');
            void Block.getBoundingClientRect();
            Block.classList.add('Pop');
        }
        // Mode badge: current drive mode in the hexagon, its neighbours on the orbit.
        const Mode = ModeName(), At = Modes.indexOf(Mode), Count = Modes.length;
        Put('Style', Find('StyleLetter'), Mode[0]);
        Put('StyleUp', Find('StyleUp'), Modes[(At + Count - 1) % Count][0]);
        Put('StyleDown', Find('StyleDown'), Modes[(At + 1) % Count][0]);
        Host.querySelectorAll('[data-sel]').forEach((Letter) => Letter.classList.toggle('On', Letter.dataset.sel === Train.Selector));
        RpmBox.classList.toggle('Hot', Hot);
    }

    return { Update };
}

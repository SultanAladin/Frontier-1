// VISOR PING
// 🧩 Squad pings in the style of a battle-royale ping wheel. Hold the middle mouse button (or F) to open an eight-way
//    wheel at the cursor, move toward a ping, and release to drop it where you aimed. A quick middle click drops a
//    context ping instead: a car is marked Enemy, the porcupine Danger, the station Need fuel, anything else Going here.
// 📝 The wheel is player input, so it is drawn on the screen; the pings it places are drawn on the windscreen.

import { Glyph, Tones } from './VisorGlass.js';

export const PingCalls = [
    { Key: 'Going',   Label: 'GOING HERE', Tone: Tones.Notice,   Glyph: 'Go' },
    { Key: 'Enemy',   Label: 'ENEMY',      Tone: Tones.Danger, Glyph: 'Enemy' },
    { Key: 'Danger',  Label: 'DANGER',     Tone: Tones.Warn,   Glyph: 'Hazard' },
    { Key: 'Supply',  Label: 'SUPPLIES',   Tone: Tones.Guide,  Glyph: 'Supply' },
    { Key: 'Fuel',    Label: 'NEED FUEL',  Tone: Tones.Warn,   Glyph: 'Pump' },
    { Key: 'Watch',   Label: 'WATCHING',   Tone: Tones.Calm,   Glyph: 'Eye' },
    { Key: 'Hold',    Label: 'HOLD HERE',  Tone: Tones.Team,   Glyph: 'Shield' },
    { Key: 'Regroup', Label: 'REGROUP',    Tone: Tones.Team,   Glyph: 'Team' },
];
export const PingByKey = Object.fromEntries(PingCalls.map((Call) => [Call.Key, Call]));

const Size   = 400;                                               // [px] - css size of the wheel canvas
const Inner  = 62, Outer = 168, Gap = 0.035;                      // [px], [rad]
const Sector = (Math.PI * 2) / PingCalls.length;

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHEEL
//------------------------------------------------------------------------------------------------------------------------

export function PingWheel()
{
    const Canvas = document.createElement('canvas');
    Canvas.className = 'PingWheel';
    const Ratio = Math.min(2, window.devicePixelRatio || 1);
    Canvas.width = Canvas.height = Size * Ratio;
    document.body.appendChild(Canvas);
    const Context = Canvas.getContext('2d');
    const State = { Open: false, X: 0, Y: 0, Selected: -1, Since: 0, Travel: 0, Aim: null, Grown: 0 };

    function Draw(Clock)
    {
        if (!State.Open) return;
        State.Grown = Math.min(1, State.Grown + 0.12);
        const Grow = 1 - Math.pow(1 - State.Grown, 3);
        Context.setTransform(Ratio, 0, 0, Ratio, 0, 0);
        Context.clearRect(0, 0, Size, Size);
        Context.translate(Size / 2, Size / 2);
        Context.scale(0.86 + Grow * 0.14, 0.86 + Grow * 0.14);
        Context.globalAlpha = Grow;

        // Backing disc.
        Context.fillStyle = 'rgba(3, 8, 14, 0.62)';
        Context.beginPath(); Context.arc(0, 0, Outer + 10, 0, Math.PI * 2); Context.arc(0, 0, Inner - 8, 0, Math.PI * 2, true); Context.fill();

        PingCalls.forEach((Call, Index) =>
        {
            const Middle = -Math.PI / 2 + Index * Sector;
            const From = Middle - Sector / 2 + Gap, To = Middle + Sector / 2 - Gap;
            const Picked = Index === State.Selected;
            Context.beginPath();
            Context.arc(0, 0, Picked ? Outer + 6 : Outer, From, To);
            Context.arc(0, 0, Inner, To, From, true);
            Context.closePath();
            Context.fillStyle = Picked ? Call.Tone : 'rgba(255,255,255,0.05)';
            Context.globalAlpha = Grow * (Picked ? 0.28 : 1);
            Context.fill();
            Context.globalAlpha = Grow;
            Context.strokeStyle = Picked ? Call.Tone : 'rgba(232,244,255,0.16)';
            Context.lineWidth = Picked ? 2.5 : 1;
            Context.shadowColor = Call.Tone;
            Context.shadowBlur = Picked ? 18 : 0;
            Context.stroke();
            Context.shadowBlur = 0;
            const Reach = (Inner + Outer) / 2 + (Picked ? 4 : 0);
            Glyph(Context, Call.Glyph, Math.cos(Middle) * Reach, Math.sin(Middle) * Reach, Picked ? 19 : 16, Picked ? Call.Tone : 'rgba(232,244,255,0.78)', 0.13);
        });

        // Centre: the picked ping (or cancel) and the aim distance.
        const Picked = PingCalls[State.Selected];
        Context.fillStyle = Picked ? Picked.Tone : 'rgba(232,244,255,0.6)';
        Context.textAlign = 'center';
        Context.font = '500 14px "DM Sans", sans-serif';
        Context.letterSpacing = '2px';
        Context.fillText(Picked ? Picked.Label : 'CANCEL', 0, -2);
        Context.font = '400 13px "DM Sans", sans-serif';
        Context.fillStyle = 'rgba(232,244,255,0.7)';
        Context.fillText(State.Aim ? `${State.Aim.Label} · ${State.Aim.Distance} m` : '', 0, 18);
        Context.letterSpacing = '0px';

        // Pointer notch toward the cursor.
        if (State.Travel > 8)
        {
            const Angle = Math.atan2(State.DY, State.DX);
            Context.fillStyle = Picked ? Picked.Tone : '#e8f4ff';
            Context.beginPath();
            Context.moveTo(Math.cos(Angle) * (Inner - 4), Math.sin(Angle) * (Inner - 4));
            Context.lineTo(Math.cos(Angle + 0.13) * (Inner - 16), Math.sin(Angle + 0.13) * (Inner - 16));
            Context.lineTo(Math.cos(Angle - 0.13) * (Inner - 16), Math.sin(Angle - 0.13) * (Inner - 16));
            Context.fill();
        }
        void Clock;
    }

    return {
        get Open() { return State.Open; },
        get Selected() { return State.Selected; },
        Begin(X, Y, Aim)
        {
            Object.assign(State, { Open: true, X, Y, Selected: -1, Since: performance.now(), Travel: 0, Aim, Grown: 0, DX: 0, DY: 0 });
            Canvas.style.left = `${X - Size / 2}px`;
            Canvas.style.top  = `${Y - Size / 2}px`;
            Canvas.classList.add('Shown');
        },
        // Returns true when the highlighted ping changed (for the hover tick).
        Move(X, Y)
        {
            if (!State.Open) return false;
            State.DX = X - State.X;
            State.DY = Y - State.Y;
            State.Travel = Math.hypot(State.DX, State.DY);
            const Before = State.Selected;
            if (State.Travel < 26) State.Selected = -1;
            else
            {
                const Angle = Math.atan2(State.DY, State.DX) + Math.PI / 2;
                State.Selected = ((Math.round(Angle / Sector) % PingCalls.length) + PingCalls.length) % PingCalls.length;
            }
            return Before !== State.Selected;
        },
        // Picks by index directly (test hook and keyboard).
        Pick(Index) { State.Selected = Index; State.Travel = 60; },
        End()
        {
            const Quick = performance.now() - State.Since < 230 && State.Travel < 14;
            const Result = { Call: PingCalls[State.Selected] ?? null, Quick, Aim: State.Aim };
            State.Open = false;
            Canvas.classList.remove('Shown');
            return Result;
        },
        Draw,
    };
}

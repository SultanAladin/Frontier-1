// VISOR GLASS
// 🧩 Paints the head-up display onto one canvas that is laid on the windscreen with additive light, so black stays
//    clear glass and every mark reads as light on the glass. Visor.js decides what to show and where; this file only
//    draws: world tags, lane guides, route chevrons, the stop bar, edge arrows, the combiner strip, cards and banners.
// 📝 Canvas space is glass space: 2048 × 960 px across the glass, left to right as the driver sees it.

export const GlassPixels = [2048, 960];

export const Tones = {
    Notice:   '#3ff0ff',
    Guide:  '#5dff7a',
    Warn:   '#ffc23a',
    Danger: '#ff3b3b',
    Team:   '#c58cff',
    Calm:   '#e8f4ff',
};

const Font = (Size, Weight = 400) => `${Weight} ${Size}px "DM Sans", system-ui, sans-serif`;
const Ease = (Value) => 1 - Math.pow(1 - Math.min(1, Math.max(0, Value)), 3);

//------------------------------------------------------------------------------------------------------------------------
//                                                    GLYPHS
//------------------------------------------------------------------------------------------------------------------------

// Every glyph is drawn centred on (0, 0) in a box of about ±1, scaled by the caller, stroked in the current style.
const Glyphs = {
    Hazard(Context)
    {
        Context.beginPath(); Context.moveTo(0, -1); Context.lineTo(1.05, 0.85); Context.lineTo(-1.05, 0.85); Context.closePath(); Context.stroke();
        Context.beginPath(); Context.moveTo(0, -0.35); Context.lineTo(0, 0.3); Context.stroke();
        Context.beginPath(); Context.arc(0, 0.58, 0.06, 0, Math.PI * 2); Context.fill();
    },
    Critter(Context)
    {
        // A porcupine in profile: arched back of spines over a small body.
        Context.beginPath(); Context.ellipse(0, 0.25, 0.85, 0.42, 0, Math.PI, 0); Context.stroke();
        for (let Spine = -4; Spine <= 4; Spine++)
        {
            const Angle = Math.PI / 2 + Spine * 0.3, X = Math.cos(Angle) * 0.85, Y = 0.25 - Math.sin(Angle) * 0.42;
            Context.beginPath(); Context.moveTo(X, Y); Context.lineTo(X * 1.25, Y - 0.34); Context.stroke();
        }
        Context.beginPath(); Context.moveTo(-0.85, 0.25); Context.lineTo(0.85, 0.25); Context.lineTo(1.05, 0.12); Context.stroke();
    },
    Signal(Context)
    {
        Context.beginPath(); Context.roundRect(-0.45, -1, 0.9, 2, 0.3); Context.stroke();
        for (const Y of [-0.58, 0, 0.58]) { Context.beginPath(); Context.arc(0, Y, 0.2, 0, Math.PI * 2); Context.stroke(); }
    },
    Ping(Context)
    {
        Context.beginPath(); Context.moveTo(0, -1); Context.lineTo(0.75, 0); Context.lineTo(0, 1); Context.lineTo(-0.75, 0); Context.closePath(); Context.stroke();
        Context.beginPath(); Context.moveTo(0, -0.45); Context.lineTo(0.34, 0); Context.lineTo(0, 0.45); Context.lineTo(-0.34, 0); Context.closePath(); Context.fill();
    },
    Pump(Context)
    {
        Context.beginPath(); Context.roundRect(-0.75, -0.95, 0.95, 1.9, 0.12); Context.stroke();
        Context.beginPath(); Context.rect(-0.55, -0.72, 0.55, 0.45); Context.stroke();
        Context.beginPath(); Context.moveTo(0.2, -0.3); Context.lineTo(0.55, -0.3); Context.lineTo(0.75, 0.55); Context.lineTo(0.92, 0.55); Context.lineTo(0.92, -0.55); Context.lineTo(0.7, -0.85); Context.stroke();
    },
    Thermo(Context)
    {
        Context.beginPath(); Context.moveTo(-0.18, 0.35); Context.lineTo(-0.18, -0.85); Context.arc(0, -0.85, 0.18, Math.PI, 0); Context.lineTo(0.18, 0.35); Context.stroke();
        Context.beginPath(); Context.arc(0, 0.6, 0.34, 0, Math.PI * 2); Context.stroke();
        for (const Y of [-0.55, -0.25, 0.05]) { Context.beginPath(); Context.moveTo(0.38, Y); Context.lineTo(0.62, Y); Context.stroke(); }
        Context.beginPath(); Context.moveTo(-0.95, 0.95); Context.bezierCurveTo(-0.6, 0.75, -0.35, 1.15, 0, 0.95); Context.bezierCurveTo(0.35, 0.75, 0.6, 1.15, 0.95, 0.95); Context.stroke();
    },
    Oxygen(Context)
    {
        // A drawn "O₂": the letter as a ring, the subscript 2 as a short path.
        Context.beginPath(); Context.ellipse(-0.3, -0.1, 0.5, 0.62, 0, 0, Math.PI * 2); Context.stroke();
        Context.beginPath(); Context.moveTo(0.42, 0.42); Context.quadraticCurveTo(0.62, 0.18, 0.82, 0.42); Context.quadraticCurveTo(0.84, 0.6, 0.42, 0.95); Context.lineTo(0.9, 0.95); Context.stroke();
    },
    Fuel(Context) { Glyphs.Pump(Context); },
    Snow(Context)
    {
        for (let Arm = 0; Arm < 3; Arm++)
        {
            const Angle = Arm * Math.PI / 3;
            Context.beginPath(); Context.moveTo(Math.cos(Angle) * -1, Math.sin(Angle) * -1); Context.lineTo(Math.cos(Angle), Math.sin(Angle)); Context.stroke();
        }
    },
    Go(Context)
    {
        // Location marker: a pin with a hollow head.
        Context.beginPath(); Context.arc(0, -0.3, 0.55, Math.PI * 0.85, Math.PI * 0.15); Context.lineTo(0, 1); Context.closePath(); Context.stroke();
        Context.beginPath(); Context.arc(0, -0.3, 0.2, 0, Math.PI * 2); Context.fill();
    },
    Enemy(Context)
    {
        // Crosshair: ring, four ticks and a centre dot.
        Context.beginPath(); Context.arc(0, 0, 0.62, 0, Math.PI * 2); Context.stroke();
        for (const [X, Y] of [[0, -1], [1, 0], [0, 1], [-1, 0]]) { Context.beginPath(); Context.moveTo(X * 0.35, Y * 0.35); Context.lineTo(X, Y); Context.stroke(); }
        Context.beginPath(); Context.arc(0, 0, 0.1, 0, Math.PI * 2); Context.fill();
    },
    Supply(Context)
    {
        // Crate with a strap.
        Context.beginPath(); Context.rect(-0.85, -0.6, 1.7, 1.4); Context.stroke();
        Context.beginPath(); Context.moveTo(-0.85, -0.15); Context.lineTo(0.85, -0.15); Context.moveTo(0, -0.6); Context.lineTo(0, 0.8); Context.stroke();
        Context.beginPath(); Context.moveTo(-0.6, -0.6); Context.lineTo(-0.35, -0.95); Context.lineTo(0.35, -0.95); Context.lineTo(0.6, -0.6); Context.stroke();
    },
    Eye(Context)
    {
        Context.beginPath(); Context.moveTo(-1, 0); Context.quadraticCurveTo(0, -0.95, 1, 0); Context.quadraticCurveTo(0, 0.95, -1, 0); Context.stroke();
        Context.beginPath(); Context.arc(0, 0, 0.3, 0, Math.PI * 2); Context.fill();
    },
    Shield(Context)
    {
        Context.beginPath(); Context.moveTo(0, -1); Context.lineTo(0.8, -0.65); Context.quadraticCurveTo(0.8, 0.55, 0, 1); Context.quadraticCurveTo(-0.8, 0.55, -0.8, -0.65); Context.closePath(); Context.stroke();
        Context.beginPath(); Context.moveTo(0, -0.55); Context.lineTo(0, 0.55); Context.stroke();
    },
    Car(Context)
    {
        Context.beginPath(); Context.moveTo(-1, 0.45); Context.lineTo(-1, 0); Context.lineTo(-0.55, -0.55); Context.lineTo(0.55, -0.55); Context.lineTo(1, 0); Context.lineTo(1, 0.45); Context.closePath(); Context.stroke();
        for (const X of [-0.55, 0.55]) { Context.beginPath(); Context.arc(X, 0.55, 0.18, 0, Math.PI * 2); Context.fill(); }
    },
    Team(Context)
    {
        Context.beginPath(); Context.arc(0, -0.45, 0.38, 0, Math.PI * 2); Context.stroke();
        Context.beginPath(); Context.moveTo(-0.8, 0.95); Context.quadraticCurveTo(-0.75, 0.05, 0, 0.05); Context.quadraticCurveTo(0.75, 0.05, 0.8, 0.95); Context.stroke();
    },
};

export function Glyph(Context, Name, X, Y, Size, Tone, Width = 0.13)
{
    Context.save();
    Context.translate(X, Y);
    Context.scale(Size, Size);
    Context.lineWidth = Width;
    Context.lineJoin = 'round';
    Context.lineCap = 'round';
    Context.strokeStyle = Context.fillStyle = Tone;
    Glyphs[Name](Context);
    Context.restore();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PAINTER
//------------------------------------------------------------------------------------------------------------------------

export function GlassPainter(Canvas)
{
    const Context = Canvas.getContext('2d');
    const [W, H] = GlassPixels;
    Canvas.width = W;
    Canvas.height = H;

    const Glow = (Tone, Blur = 16) => { Context.shadowColor = Tone; Context.shadowBlur = Blur; };
    const Text = (Value, X, Y, Size, Tone, { Weight = 400, Align = 'left', Sill = 'alphabetic', Alpha = 1, Track = 0 } = {}) =>
    {
        Context.save();
        Context.globalAlpha *= Alpha;
        Context.font = Font(Size, Weight);
        Context.textAlign = Align;
        Context.textBaseline = Sill;
        if (Track) Context.letterSpacing = `${Track}px`;
        Context.fillStyle = Tone;
        Glow(Tone, Size * 0.35);
        Context.fillText(Value, X, Y);
        Context.restore();
    };

    // World tag: bracket ring on the target, a stem, and a label plate with glyph, title, figure and subline.
    function Tag(Piece)
    {
        const { X, Y, Tone, Scale = 1, Alpha = 1 } = Piece;
        Context.save();
        Context.globalAlpha = Alpha;
        Context.strokeStyle = Context.fillStyle = Tone;
        Glow(Tone, 14);
        const Ring = (Piece.Ring ?? 30) * Scale;
        Context.lineWidth = 3;
        if (Piece.Shape === 'Hazard' || Piece.Shape === 'Signal')
        {
            // Corner brackets around the target, breathing slightly.
            const Pulse = 1 + Math.sin(Piece.Clock * (Piece.Shape === 'Hazard' ? 9 : 4)) * 0.06;
            const Half = Ring * Pulse, Arm = Half * 0.45;
            for (const [SX, SY] of [[-1, -1], [1, -1], [1, 1], [-1, 1]])
            {
                Context.beginPath();
                Context.moveTo(X + SX * Half, Y + SY * (Half - Arm)); Context.lineTo(X + SX * Half, Y + SY * Half); Context.lineTo(X + SX * (Half - Arm), Y + SY * Half);
                Context.stroke();
            }
        }
        // Stem and chevrons pointing down at the target.
        const Lift = Ring + 46 * Scale;
        for (let Chevron = 0; Chevron < 2; Chevron++)
        {
            const CY = Y - Ring - 10 * Scale - Chevron * 13 * Scale;
            Context.globalAlpha = Alpha * (0.5 + 0.5 * Math.max(0, Math.sin(Piece.Clock * 6 - Chevron * 1.2)));
            Context.beginPath(); Context.moveTo(X - 11 * Scale, CY - 7 * Scale); Context.lineTo(X, CY); Context.lineTo(X + 11 * Scale, CY - 7 * Scale); Context.stroke();
        }
        Context.globalAlpha = Alpha;
        // The plate stacks upward from the top of the stem: subline, distance, title, glyph.
        let Line = Y - Lift - 8 * Scale;
        if (Piece.Sub) { Text(Piece.Sub, X, Line, 18 * Scale, Tone, { Weight: 500, Align: 'center', Track: 2 * Scale, Alpha: Alpha * 0.9 }); Line -= 26 * Scale; }
        if (Piece.Big) { Text(Piece.Big, X, Line, 42 * Scale, Tone, { Align: 'center', Alpha }); Line -= 44 * Scale; }
        if (Piece.Title) { Text(Piece.Title, X, Line, 22 * Scale, Tone, { Weight: 500, Align: 'center', Track: 3 * Scale, Alpha }); Line -= 22 * Scale; }
        Glyph(Context, Piece.Glyph, X, Line - 26 * Scale, 24 * Scale, Tone, 0.12);
        Context.restore();
    }

    // Countdown ring for the traffic signal: the arc empties as the phase runs out.
    function SignalDial(Piece)
    {
        const { X, Y, Tone, Scale = 1 } = Piece;
        Context.save();
        Glow(Tone, 18);
        Context.strokeStyle = Tone;
        const Radius = 46 * Scale;
        Context.globalAlpha = 0.25; Context.lineWidth = 5 * Scale;
        Context.beginPath(); Context.arc(X, Y, Radius, 0, Math.PI * 2); Context.stroke();
        Context.globalAlpha = 1; Context.lineWidth = 6 * Scale;
        Context.beginPath(); Context.arc(X, Y, Radius, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * Piece.Share); Context.stroke();
        Context.restore();
        Text(Piece.Big, X, Y + 2 * Scale, 34 * Scale, Tone, { Weight: 500, Align: 'center', Sill: 'middle' });
        Text(Piece.Title, X, Y + Radius + 30 * Scale, 20 * Scale, Tone, { Weight: 500, Align: 'center', Track: 3 * Scale });
        if (Piece.Sub) Text(Piece.Sub, X, Y + Radius + 56 * Scale, 17 * Scale, Tone, { Align: 'center', Alpha: 0.85 });
    }

    // Car box: corner brackets around a car's outline on the glass and a compact label above (like the floating car tags).
    function CarBox(Piece)
    {
        const { Left, Top, Right, Bottom, Tone, Alpha = 1 } = Piece;
        const Arm = Math.max(6, Math.min(22, (Right - Left) * 0.22));
        Context.save();
        Context.globalAlpha = Alpha;
        Context.strokeStyle = Tone;
        Glow(Tone, 12);
        Context.lineWidth = Piece.Strong ? 3.2 : 2;
        for (const [X, Y, SX, SY] of [[Left, Top, 1, 1], [Right, Top, -1, 1], [Right, Bottom, -1, -1], [Left, Bottom, 1, -1]])
        {
            Context.beginPath(); Context.moveTo(X, Y + SY * Arm); Context.lineTo(X, Y); Context.lineTo(X + SX * Arm, Y); Context.stroke();
        }
        Context.restore();
        const Centre = (Left + Right) / 2;
        if (Piece.Title) Text(Piece.Title, Centre, Top - (Piece.Sub ? 34 : 12), Piece.Strong ? 22 : 17, Tone, { Weight: 500, Align: 'center', Track: 2, Alpha });
        if (Piece.Sub) Text(Piece.Sub, Centre, Top - 12, Piece.Strong ? 20 : 16, Tone, { Align: 'center', Alpha: Alpha * 0.9 });
    }

    // Squad ping: a dot on the spot, a short stem, the kind glyph in a diamond, label, distance and who acknowledged it.
    function Mark(Piece)
    {
        const { X, Y, Tone, Age, Alpha = 1 } = Piece;
        Context.save();
        Context.globalAlpha = Alpha;
        Context.strokeStyle = Context.fillStyle = Tone;
        Glow(Tone, 16);
        if (Age < 1.1)
        {
            // Placement pulse: two rings racing out from the spot.
            for (const Delay of [0, 0.25])
            {
                const Grow = Math.max(0, Math.min(1, (Age - Delay) / 0.8));
                if (Grow <= 0 || Grow >= 1) continue;
                Context.globalAlpha = Alpha * (1 - Grow);
                Context.lineWidth = 3;
                Context.beginPath(); Context.arc(X, Y, 10 + Grow * 70, 0, Math.PI * 2); Context.stroke();
            }
            Context.globalAlpha = Alpha;
        }
        Context.beginPath(); Context.arc(X, Y, 5, 0, Math.PI * 2); Context.fill();
        Context.lineWidth = 2;
        Context.beginPath(); Context.moveTo(X, Y - 8); Context.lineTo(X, Y - 34); Context.stroke();
        const HeadY = Y - 62;
        Context.lineWidth = 2.5;
        Context.beginPath(); Context.moveTo(X, HeadY - 26); Context.lineTo(X + 26, HeadY); Context.lineTo(X, HeadY + 26); Context.lineTo(X - 26, HeadY); Context.closePath(); Context.stroke();
        Context.globalAlpha = Alpha * 0.12; Context.fill(); Context.globalAlpha = Alpha;
        Context.restore();
        Glyph(Context, Piece.Glyph, X, HeadY, 12, Tone, 0.15);
        Text(Piece.Title, X, HeadY - 38, 19, Tone, { Weight: 500, Align: 'center', Track: 2, Alpha });
        Text(Piece.Big, X + 36, HeadY + 8, 22, Tone, { Alpha });
        if (Piece.Sub) Text(Piece.Sub, X, Y + 28, 15, Tone, { Weight: 500, Align: 'center', Track: 1.5, Alpha: Alpha * 0.85 });
    }

    // Polyline guides on the road (lane edges, stop bar): drawn as glowing strokes that fade with distance.
    function Guide(Line)
    {
        if (Line.Points.length < 2) return;
        Context.save();
        Context.strokeStyle = Line.Tone;
        Glow(Line.Tone, 12);
        Context.lineCap = 'round';
        for (let Each = 1; Each < Line.Points.length; Each++)
        {
            const [AX, AY, AFade] = Line.Points[Each - 1], [BX, BY] = Line.Points[Each];
            Context.globalAlpha = (Line.Alpha ?? 1) * AFade;
            Context.lineWidth = Line.Width * (0.4 + AFade * 0.6);
            Context.beginPath(); Context.moveTo(AX, AY); Context.lineTo(BX, BY); Context.stroke();
        }
        Context.restore();
    }

    // Route chevrons painted on the road, each already projected (position, scale, heading on the glass).
    function Chevron(Piece)
    {
        Context.save();
        Context.translate(Piece.X, Piece.Y);
        Context.rotate(Piece.Angle);
        Context.scale(Piece.Scale, Piece.Scale * 0.55);
        Context.globalAlpha = Piece.Alpha;
        Context.strokeStyle = Piece.Tone;
        Glow(Piece.Tone, 10);
        Context.lineWidth = 7;
        Context.lineJoin = 'round';
        Context.beginPath(); Context.moveTo(-26, 14); Context.lineTo(0, -14); Context.lineTo(26, 14); Context.stroke();
        Context.restore();
    }

    // Arrow on the glass edge for a target outside the glass (teammate behind a pillar, station off to the side).
    function Edge(Piece)
    {
        Context.save();
        Context.translate(Piece.X, Piece.Y);
        Context.rotate(Piece.Angle);
        Context.fillStyle = Context.strokeStyle = Piece.Tone;
        Glow(Piece.Tone, 14);
        Context.beginPath(); Context.moveTo(22, 0); Context.lineTo(-10, -16); Context.lineTo(-4, 0); Context.lineTo(-10, 16); Context.closePath(); Context.fill();
        Context.restore();
        Text(Piece.Text, Piece.X - Math.cos(Piece.Angle) * 46, Piece.Y - Math.sin(Piece.Angle) * 46 + 7, 19, Piece.Tone, { Weight: 500, Align: 'center' });
    }

    // Combiner strip: the fixed panel low in the driver's view (mini lane map, speed, next turn, clock and gauges).
    function Combiner(Picture, Centre, Sill)
    {
        const Tone = Picture.RimTone;
        const Wide = 660, Tall = 150, Lean = 46;
        const Left = Centre - Wide / 2, Top = Sill - Tall;
        Context.save();
        Context.strokeStyle = Tone;
        Glow(Tone, 22);
        Context.lineWidth = 3.5;
        Context.globalAlpha = 0.95;
        Context.beginPath();
        Context.moveTo(Left, Sill); Context.lineTo(Left + Lean, Top); Context.lineTo(Left + Wide - Lean, Top); Context.lineTo(Left + Wide, Sill);
        Context.stroke();
        Context.restore();

        // Mini map: lane grid with the route line (bends right when the station route is on).
        const MapX = Left + 92, MapY = Sill - 70;
        Context.save();
        Context.strokeStyle = Tones.Calm;
        Context.globalAlpha = 0.65;
        Context.lineWidth = 2.5;
        for (const Offset of [-42, 0, 42]) { Context.beginPath(); Context.moveTo(MapX + Offset, MapY - 46); Context.lineTo(MapX + Offset * 1.25, MapY + 46); Context.stroke(); }
        for (const Offset of [-22, 14]) { Context.beginPath(); Context.moveTo(MapX - 62, MapY + Offset); Context.lineTo(MapX + 62, MapY + Offset); Context.stroke(); }
        Context.globalAlpha = 1;
        Context.strokeStyle = Tones.Guide;
        Glow(Tones.Guide, 14);
        Context.lineWidth = 6;
        Context.lineCap = 'round';
        Context.beginPath();
        Context.moveTo(MapX + 10, MapY + 46);
        if (Picture.Nav.Glyph === 'Right') { Context.lineTo(MapX + 10, MapY + 2); Context.quadraticCurveTo(MapX + 12, MapY - 18, MapX + 56, MapY - 22); }
        else Context.lineTo(MapX + 10, MapY - 46);
        Context.stroke();
        Context.fillStyle = Tones.Guide;
        Context.beginPath(); Context.arc(MapX + 10, MapY + 40, 6, 0, Math.PI * 2); Context.fill();
        Context.restore();

        // Speed.
        Text(String(Math.round(Picture.Speed)), Centre - 10, Sill - 52, 76, Tones.Calm, { Weight: 400, Align: 'right' });
        Text('km/h', Centre - 2, Sill - 54, 22, Tones.Calm, { Alpha: 0.8 });
        if (Picture.Auto) Text(Picture.Auto, Centre - 46, Sill - 22, 16, Tones.Notice, { Align: 'center', Track: 2, Alpha: 0.9 });

        // Next manoeuvre.
        const NavX = Centre + 112, NavY = Sill - 78;
        Context.save();
        Context.strokeStyle = Tones.Calm;
        Glow(Tones.Calm, 12);
        Context.lineWidth = 5;
        Context.lineCap = Context.lineJoin = 'round';
        Context.beginPath();
        if (Picture.Nav.Glyph === 'Right') { Context.moveTo(NavX - 8, NavY + 26); Context.lineTo(NavX - 8, NavY); Context.quadraticCurveTo(NavX - 8, NavY - 16, NavX + 14, NavY - 16); Context.moveTo(NavX + 4, NavY - 28); Context.lineTo(NavX + 16, NavY - 16); Context.lineTo(NavX + 4, NavY - 4); }
        else { Context.moveTo(NavX, NavY + 26); Context.lineTo(NavX, NavY - 24); Context.moveTo(NavX - 13, NavY - 11); Context.lineTo(NavX, NavY - 24); Context.lineTo(NavX + 13, NavY - 11); }
        Context.stroke();
        Context.restore();
        Text(Picture.Nav.Text, NavX + 36, Sill - 52, 56, Tones.Calm, { Weight: 400 });

        // Clock, outside temperature and altitude under the strip.
        Text(Picture.Footer, Centre, Sill + 34, 20, Tones.Calm, { Align: 'center', Alpha: 0.8, Track: 1 });

        // Gauges along the right end: fuel, coolant, cabin oxygen (each lights in its warning tone).
        const GaugeX = Left + Wide + 40;
        Picture.Gauges.forEach((Gauge, Index) =>
        {
            const GY = Sill - 112 + Index * 46;
            Glyph(Context, Gauge.Glyph, GaugeX, GY, 13, Gauge.Tone, 0.15);
            Text(Gauge.Text, GaugeX + 26, GY + 8, 22, Gauge.Tone, { Weight: 500 });
        });

        // Advisory line above the strip (overtake clear / wait), when there is no BRAKE.
        if (Picture.Advisory && !Picture.Brake) Text(Picture.Advisory.Text, Centre, Top - 22, 26, Picture.Advisory.Tone, { Weight: 500, Align: 'center', Track: 4 });

        // BRAKE warning above the strip, flashing.
        if (Picture.Brake)
        {
            const Flash = Math.sin(Picture.Clock * 14) > -0.3 ? 1 : 0.25;
            Text('BRAKE', Centre, Top - 18, 64, Tones.Danger, { Weight: 500, Align: 'center', Track: 10, Alpha: Flash });
        }
    }

    // Notification card: glyph, title, subline, key hint and a lifetime bar; slides in from the right.
    function Card(Piece, X, Y)
    {
        const In = Ease(Piece.Age / 0.45), Out = Piece.Leaving ? 1 - Ease(Piece.Leaving / 0.4) : 1;
        const Alpha = In * Out;
        if (Alpha <= 0.01) return 0;
        const Shift = (1 - In) * 70;
        Context.font = Font(19);
        const SubWide = Context.measureText(Piece.Sub ?? '').width;
        Context.font = Font(24, 500);
        const Wide = Math.max(440, Math.min(560, Math.max(SubWide, Context.measureText(Piece.Title).width) + 120)), Tall = Piece.Hint ? 118 : 92;
        Context.save();
        Context.globalAlpha = Alpha;
        Context.translate(X + Shift, Y);
        Context.strokeStyle = Context.fillStyle = Piece.Tone;
        Glow(Piece.Tone, 16);
        Context.lineWidth = 2.5;
        Context.globalAlpha = Alpha * 0.9;
        Context.beginPath(); Context.roundRect(0, 0, Wide, Tall, 16); Context.stroke();
        Context.globalAlpha = Alpha * 0.07;
        Context.fill();
        Context.globalAlpha = Alpha;
        Context.fillRect(0, 18, 4, Tall - 36);
        if (Piece.Life) { Context.globalAlpha = Alpha * 0.6; Context.fillRect(20, Tall - 8, (Wide - 40) * Math.max(0, 1 - Piece.Age / Piece.Life), 2.5); Context.globalAlpha = Alpha; }
        Context.restore();
        Glyph(Context, Piece.Glyph, X + Shift + 46, Y + 46, 20, Piece.Tone, 0.12);
        Context.save(); Context.globalAlpha = Alpha;
        Text(Piece.Title, X + Shift + 88, Y + 40, 24, Piece.Tone, { Weight: 500, Track: 1 });
        Text(Piece.Sub, X + Shift + 88, Y + 70, 19, Tones.Calm, { Alpha: 0.85 });
        if (Piece.Hint) Text(Piece.Hint, X + Shift + 88, Y + 100, 17, Piece.Tone, { Weight: 500, Track: 2, Alpha: 0.9 });
        Context.restore();
        return (Tall + 16) * Alpha;
    }

    // Banner: the one critical message, centred above the driver's line of sight.
    function Banner(Piece, Centre)
    {
        const Pulse = 0.75 + 0.25 * Math.sin(Piece.Clock * 6);
        Context.save();
        Context.globalAlpha = Pulse;
        Context.strokeStyle = Piece.Tone;
        Glow(Piece.Tone, 20);
        Context.lineWidth = 3;
        const Wide = 560;
        Context.beginPath(); Context.moveTo(Centre - Wide / 2, 214); Context.lineTo(Centre - Wide / 2 + 26, 150); Context.lineTo(Centre + Wide / 2 - 26, 150); Context.lineTo(Centre + Wide / 2, 214); Context.stroke();
        Context.restore();
        Glyph(Context, Piece.Glyph, Centre - Wide / 2 + 70, 186, 20, Piece.Tone, 0.12);
        Text(Piece.Text, Centre + 20, 194, 34, Piece.Tone, { Weight: 500, Align: 'center', Track: 4 });
        Text(Piece.Sub, Centre + 20, 246, 21, Tones.Calm, { Align: 'center', Alpha: 0.85 });
    }

    return {
        Paint(Picture)
        {
            Context.setTransform(1, 0, 0, 1, 0, 0);
            Context.globalAlpha = 1;
            Context.shadowBlur = 0;
            Context.fillStyle = '#000';
            Context.fillRect(0, 0, W, H);
            for (const Line of Picture.Guides) Guide(Line);
            for (const Piece of Picture.Chevrons) Chevron(Piece);
            for (const Piece of Picture.Cars ?? []) CarBox(Piece);
            for (const Piece of Picture.Tags) (Piece.Shape === 'Signal' ? SignalDial : Piece.Shape === 'Mark' ? Mark : Tag)(Piece);
            for (const Piece of Picture.Edges) Edge(Piece);
            Combiner(Picture, Picture.Centre, H - 70);
            let Y = Picture.CardTop;
            for (const Piece of Picture.Cards) Y += Card(Piece, Picture.CardLeft, Y);
            if (Picture.Banner) Banner(Picture.Banner, Picture.Centre);
        },
    };
}

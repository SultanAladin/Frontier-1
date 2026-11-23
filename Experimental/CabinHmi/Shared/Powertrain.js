// POWERTRAIN
// 🧩 A small self-timed engine and gearbox model that feeds every instrument cluster: engine speed, road speed, gear,
//    boost, fuel, coolant, hybrid charge, longitudinal and lateral g. It runs on wall-clock time, so the gauges move
//    at true pace even when the 3D scene renders slowly.
// 📝 Selector P R N D M. D shifts itself; M shifts on paddles and bounces on the limiter. A demo programme loops
//    launch → shifts → bend → cruise → brake → stop so the clusters can be watched hands-free.

const Tau = Math.PI * 2;
const Clamp = (Value, Low, High) => Math.min(High, Math.max(Low, Value));

export const DriveStyles = {
    Eco:     { Name: 'Eco',     Short: 'E', ShiftAt: 0.48, Response: 2.0, Boost: 0.5 },
    Comfort: { Name: 'Comfort', Short: 'C', ShiftAt: 0.62, Response: 3.0, Boost: 0.7 },
    Sport:   { Name: 'Sport',   Short: 'S', ShiftAt: 0.86, Response: 6.0, Boost: 1.0 },
    Track:   { Name: 'Track',   Short: 'T', ShiftAt: 0.97, Response: 9.0, Boost: 1.15 },
};

export class Powertrain
{
    /**
     * @param {{ Ratios?: number[], Final?: number, Idle?: number, Redline?: number, Limit?: number, Mass?: number,
     *           PeakTorque?: number, TyreRadius?: number, Reverse?: number, Style?: string, Tank?: number }} Options
     */
    constructor({ Ratios = [3.5, 2.35, 1.72, 1.32, 1.06, 0.87, 0.72], Final = 3.55, Idle = 820, Redline = 6800, Limit = 7300,
                  Mass = 1450, PeakTorque = 560, TyreRadius = 0.34, Reverse = 3.2, Style = 'Sport', Tank = 64 } = {})
    {
        Object.assign(this, { Ratios, Final, Idle, Redline, Limit, Mass, PeakTorque, TyreRadius, Reverse, Tank });
        this.Style      = Style;
        this.Selector   = 'P';
        this.Gear       = 1;
        this.Rpm        = Idle;
        this.Velocity   = 0;                                       // [m/s] - signed, negative in reverse
        this.Throttle   = 0;                                       // 0..1 eased
        this.Brake      = 0;                                       // 0..1 eased
        this.ThrottleGoal = 0;
        this.BrakeGoal  = 0;
        this.Steer      = 0;                                       // -1..1 (demo bends)
        this.Boost      = 0;                                       // [bar]
        this.Fuel       = 0.64;                                    // tank share 0..1
        this.Flow       = 0.6;                                     // [L/h] - instantaneous fuel flow
        this.Used       = 17.9;                                    // [L] - burnt since the trip reset (seeds 7.2 L/100 km)
        this.Coolant    = 74;                                      // [°C]
        this.Charge     = 0.62;                                    // hybrid cell 0..1
        this.Eco        = 0.8;
        this.GLong      = 0;
        this.GLat       = 0;
        this.GMax       = 0;
        this.Odometer   = 24871.4;                                 // [km]
        this.Trip       = 248.7;                                   // [km]
        this.ShiftCount = 0;                                       // increments on every gear change (UI pops)
        this.ShiftCut   = 0;                                       // [s] - torque cut left
        this.Limiter    = false;
        this.Demo       = false;
        this.DemoClock  = 0;
        this.SweepClock = -1;                                      // [s] - ignition sweep progress, -1 when idle
        this.Last       = performance.now();
    }

    get Kmh() { return Math.abs(this.Velocity) * 3.6; }
    get Litres() { return this.Fuel * this.Tank; }
    get Average() { return this.Trip > 0.2 ? this.Used / this.Trip * 100 : 7.2; }              // [L/100 km] - trip mean
    get Instant() { return this.Kmh > 3 ? Math.min(30, this.Flow / this.Kmh * 100) : null; }  // [L/100 km] - null when standing
    get Range() { return this.Litres / Math.max(3, this.Average) * 100; }                      // [km]
    get Ratio() { return this.Selector === 'R' ? this.Reverse : this.Ratios[this.Gear - 1]; }
    get GearLabel() { return this.Selector === 'D' || this.Selector === 'M' ? String(this.Gear) : this.Selector; }

    SetSelector(Selector)
    {
        if (!['P', 'R', 'N', 'D', 'M'].includes(Selector)) return;
        if ((Selector === 'P' || Selector === 'R') && this.Kmh > 6) return;    // ⚠️ refuse park or reverse while rolling
        if (Selector === 'D' && this.Selector !== 'M') this.Gear = 1;
        this.Selector = Selector;
        this.ShiftCount++;
    }

    SetStyle(Style) { if (DriveStyles[Style]) this.Style = Style; }

    Paddle(Direction)
    {
        if (this.Selector === 'D') this.Selector = 'M';
        if (this.Selector !== 'M') return;
        const Next = Clamp(this.Gear + Direction, 1, this.Ratios.length);
        if (Next === this.Gear) return;
        // A downshift that would over-rev is refused, like a real gearbox.
        if (Direction < 0 && this.EngineFor(Next) > this.Limit) return;
        this.Gear = Next;
        this.ShiftCount++;
        this.ShiftCut = 0.12;
    }

    Sweep() { this.SweepClock = 0; }

    EngineFor(Gear) { return (Math.abs(this.Velocity) / this.TyreRadius) * this.Ratios[Gear - 1] * this.Final * 60 / Tau; }

    Torque(Rpm)
    {
        const Shape = 1 - Math.pow((Rpm - 4600) / 4600, 2);
        return this.PeakTorque * Clamp(0.35 + 0.65 * Shape, 0.2, 1);
    }

    // Demo programme: 34 s loop.
    DriveDemo(Seconds)
    {
        const Time = Seconds % 34;
        if (this.Selector === 'P' || this.Selector === 'N') this.SetSelector('D');
        if (Time < 12)       { this.ThrottleGoal = 1;    this.BrakeGoal = 0; this.Steer = 0; }
        else if (Time < 15)  { this.ThrottleGoal = 0;    this.BrakeGoal = 0.6; this.Steer = 0; }
        else if (Time < 21)  { this.ThrottleGoal = 0.5;  this.BrakeGoal = 0; this.Steer = Math.sin((Time - 15) / 6 * Math.PI) * 1.1; }
        else if (Time < 25)  { this.ThrottleGoal = 0.38; this.BrakeGoal = 0; this.Steer = -Math.sin((Time - 21) / 4 * Math.PI) * 0.8; }
        else if (Time < 30)  { this.ThrottleGoal = 0;    this.BrakeGoal = 0.62; this.Steer = 0; }
        else                 { this.ThrottleGoal = 0;    this.BrakeGoal = 0.5; this.Steer = 0; }
    }

    Step()
    {
        const Now = performance.now();
        let Left  = Math.min(0.6, (Now - this.Last) / 1000);
        this.Last = Now;
        while (Left > 0) { const Slice = Math.min(0.02, Left); this.Advance(Slice); Left -= Slice; }
        return this;
    }

    Advance(Delta)
    {
        const Style = DriveStyles[this.Style];
        if (this.Demo) { this.DemoClock += Delta; this.DriveDemo(this.DemoClock); }
        const Response = 1 - Math.exp(-Delta * Style.Response);
        this.Throttle += (this.ThrottleGoal - this.Throttle) * Response;
        this.Brake    += (this.BrakeGoal - this.Brake) * (1 - Math.exp(-Delta * 6));
        this.ShiftCut  = Math.max(0, this.ShiftCut - Delta);

        const Coupled  = this.Selector === 'D' || this.Selector === 'M' || this.Selector === 'R';
        const Previous = this.Velocity;
        let Drive = 0;
        if (Coupled)
        {
            const WheelRpm = (Math.abs(this.Velocity) / this.TyreRadius) * this.Ratio * this.Final * 60 / Tau;
            const Slip     = (this.Idle + this.Throttle * 2600) * Clamp(1 - Math.abs(this.Velocity) / 6, 0, 1);   // clutch slip while pulling away
            const Target   = Math.max(this.Idle, WheelRpm + Slip);
            this.Rpm += (Target - this.Rpm) * (1 - Math.exp(-Delta * 14));
            this.Limiter = this.Rpm >= this.Limit;
            if (this.Limiter) this.Rpm = this.Limit - 180 * Math.random();
            const Cut = this.ShiftCut > 0 || this.Limiter ? 0 : 1;
            Drive = Math.min(11, (this.Torque(this.Rpm) * this.Throttle * this.Ratio * this.Final / this.TyreRadius) / this.Mass) * Cut;   // traction cap ≈ 1.1 g
            // Idle creep: an automatic edges forward (or back) at walking pace with both feet off.
            if (this.Throttle < 0.05 && this.Brake < 0.08 && this.Selector !== 'M') Drive = Math.max(Drive, Clamp((1.7 - Math.abs(this.Velocity)) * 1.2, 0, 1.4));
            if (this.Selector === 'R') Drive = -Drive * (this.Throttle < 0.05 ? 1 : 0.5);
        }
        else
        {
            const Target = this.Selector === 'P' || this.Selector === 'N' ? this.Idle + this.Throttle * (this.Limit - this.Idle) : this.Idle;
            this.Rpm += (Target - this.Rpm) * (1 - Math.exp(-Delta * (Target > this.Rpm ? 9 : 4)));
            this.Limiter = this.Rpm >= this.Limit - 60;
            if (this.Limiter) this.Rpm -= 300 * Math.random();
        }

        const Speed = Math.abs(this.Velocity), Sign = Math.sign(this.Velocity) || 1;
        const Drag  = 0.00072 * Speed * Speed + 0.16 + (Coupled && this.Throttle < 0.05 ? 0.5 : 0);    // air, rolling, engine braking
        const Resist = Drag + this.Brake * 9.0;
        if (this.Selector === 'P') this.Velocity *= Math.exp(-Delta * 8);
        else if (Math.abs(Drive) < Resist && Speed <= Resist * Delta) this.Velocity = 0;           // friction stops the car dead
        else this.Velocity += (Drive - Sign * Resist) * Delta;
        if (this.Selector === 'R') this.Velocity = Math.max(this.Velocity, -7);
        if (this.Selector !== 'R' && this.Velocity < 0) this.Velocity = 0;
        

        // Automatic shifting.
        if (this.Selector === 'D' && this.ShiftCut <= 0)
        {
            const ShiftRpm = this.Redline * (Style.ShiftAt * (0.55 + 0.45 * this.Throttle));
            if (this.Rpm > Math.max(2300, ShiftRpm) && this.Gear < this.Ratios.length) { this.Gear++; this.ShiftCount++; this.ShiftCut = 0.18; }
            else if (this.Gear > 1 && this.EngineFor(this.Gear) < 1500 + this.Throttle * 1500 && this.EngineFor(this.Gear - 1) < ShiftRpm * 0.85) { this.Gear--; this.ShiftCount++; this.ShiftCut = 0.1; }
        }

        // Derived readings.
        const Long  = (this.Velocity - Previous) / Delta / 9.81;
        this.GLong += (Long - this.GLong) * (1 - Math.exp(-Delta * 6));
        const Lat   = this.Steer * Math.min(1, Speed / 18) * 0.85;
        this.GLat  += (Lat - this.GLat) * (1 - Math.exp(-Delta * 4));
        this.GMax   = Math.max(this.GMax, Math.hypot(this.GLong, this.GLat));
        const BoostGoal = Clamp(this.Throttle * (this.Rpm - 1400) / 3200, 0, 1) * 2.4 * Style.Boost;
        this.Boost += (BoostGoal - this.Boost) * (1 - Math.exp(-Delta * 5));
        this.Coolant += ((88 + this.Throttle * 9) - this.Coolant) * (1 - Math.exp(-Delta * 0.08));
        // Fuel: idle trickle plus a load term that scales with engine speed and throttle (≈ 6 L/100 km at a steady 100).
        const Running = this.Rpm > 200 ? 1 : 0;
        this.Flow   = Running * (0.6 + (this.Rpm / 1000) * (0.3 + this.Throttle * 7) * (0.8 + 0.2 * Style.Boost));
        const Burnt = this.Flow * Delta / 3600;
        this.Used  += Burnt;
        this.Fuel   = Math.max(0.03, this.Fuel - Burnt / this.Tank);
        this.Charge = Clamp(this.Charge + (this.Brake * Speed * 0.00009 - this.Throttle * 0.00004) * Delta * 60, 0.05, 1);
        this.Eco   += ((1 - this.Throttle * 0.9 - this.Brake * 0.2) - this.Eco) * (1 - Math.exp(-Delta * 2));
        this.Odometer += Speed * Delta / 1000;
        this.Trip     += Speed * Delta / 1000;
        if (this.SweepClock >= 0) { this.SweepClock += Delta; if (this.SweepClock > 1.7) this.SweepClock = -1; }
    }

    // 📝 Ignition sweep: 0 → 1 → 0 over 1.7 s; gauges add it on top of their live value.
    get SweepLevel()
    {
        if (this.SweepClock < 0) return 0;
        const Time = this.SweepClock;
        return Time < 0.7 ? Math.sin((Time / 0.7) * Math.PI / 2) : Time < 0.9 ? 1 : Math.cos(((Time - 0.9) / 0.8) * Math.PI / 2);
    }

    // Distance travelled since the last call (wheel spin, floor streaks).
    TakeMetres(Store) { const Now = this.Odometer; const Moved = (Now - (Store.Last ?? Now)) * 1000; Store.Last = Now; return Moved * Math.sign(this.Velocity || 1); }
}

// Keyboard: ↑ throttle, ↓ brake, Q / E paddles, P R N D M selector.
export function BindPowertrainKeys(Train, Active = () => true)
{
    window.addEventListener('keydown', (Press) =>
    {
        if (!Active()) return;
        if (Press.key === 'ArrowUp') Train.ThrottleGoal = 1;
        else if (Press.key === 'ArrowDown') Train.BrakeGoal = 1;
        else if (Press.key === 'q' || Press.key === 'Q') Train.Paddle(-1);
        else if (Press.key === 'e' || Press.key === 'E') Train.Paddle(1);
        else if ('PRNDM'.includes(Press.key.toUpperCase()) && Press.key.length === 1) Train.SetSelector(Press.key.toUpperCase());
    });
    window.addEventListener('keyup', (Press) =>
    {
        if (Press.key === 'ArrowUp') Train.ThrottleGoal = 0;
        if (Press.key === 'ArrowDown') Train.BrakeGoal = 0;
    });
}

// Press-and-hold binding for on-screen pedals.
export function BindHold(Element, OnDown, OnUp)
{
    Element.addEventListener('pointerdown', (Press) => { Element.setPointerCapture(Press.pointerId); Element.classList.add('Held'); OnDown(); });
    const Release = () => { Element.classList.remove('Held'); OnUp(); };
    Element.addEventListener('pointerup', Release);
    Element.addEventListener('pointercancel', Release);
}

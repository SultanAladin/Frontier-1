import { ChassisSheet } from "./VehicleSpecification.js";
//============================================================================================================================================
//                                                         DRIVERINPUTINTEGRATOR.JS
//============================================================================================================================================
// 🧩 Keyboard samples ramped into the smooth throttle, brake and steer command the vehicle solver consumes.

// 📝 DriverInputIntegrator.h's keyboard path: digital keys ramp in and decay out at the Frontier rates, steering builds
//    toward ±1 while A/D are held and self-centres when released. Layout: W throttle, S brake / reverse, A/D steer,
//    Space handbrake, R respawn — Project-Drive's README controls. Arrow keys mirror WASD.
const InputConfiguration = Object.freeze({
    ThrottleRiseRate: 3.0,   // [1/s]
    ThrottleFallRate: 6.0,   // [1/s]
    BrakeRiseRate:    4.0,   // [1/s]
    BrakeFallRate:    8.0,   // [1/s]
    SteerRate:        3.5,   // [1/s]
    SteerReturnRate:  5.0    // [1/s]
});

function Ramp(Current, Target, Rise, Fall, Δτ)
{
    if (Target > Current)
        return Math.min(Target, Current + Rise * Δτ);
    return Math.max(Target, Current - Fall * Δτ);
}

export class DriverInputIntegrator
{
    constructor()
    {
        this.Throttle = 0.0;
        this.Brake    = 0.0;
        this.Steer    = 0.0;
        this.Correction = 0;
    }

    Advance(Keys, Δτ, Feedback = null, Assist = {})
    {
        const Forward  = Keys.has("KeyW") || Keys.has("ArrowUp");
        const Backward = Keys.has("KeyS") || Keys.has("ArrowDown");
        const Left     = Keys.has("KeyA") || Keys.has("ArrowLeft");
        const Right    = Keys.has("KeyD") || Keys.has("ArrowRight");
        this.Throttle = Ramp(this.Throttle, Forward ? 1.0 : 0.0, InputConfiguration.ThrottleRiseRate, InputConfiguration.ThrottleFallRate, Δτ);
        this.Brake    = Ramp(this.Brake, Backward ? 1.0 : 0.0, InputConfiguration.BrakeRiseRate, InputConfiguration.BrakeFallRate, Δτ);
        const SteerTarget = (Left ? 1.0 : 0.0) - (Right ? 1.0 : 0.0);
        if (Left || Right)
            this.Steer += (SteerTarget - this.Steer) * Math.min(1.0, Δτ * InputConfiguration.SteerRate);
        else
            this.Steer += (0.0 - this.Steer) * Math.min(1.0, Δτ * InputConfiguration.SteerReturnRate);
        const Correction = ResolveCountersteer(Feedback, this.Steer, Assist);
        const Target = Assist.Enabled ? Correction : 0;
        const Limit = 1.5 * Math.max(0, Δτ);
        this.Correction = Math.max(this.Correction - Limit, Math.min(this.Correction + Limit, Target));
        // Off/reverse/airborne must immediately restore unassisted input.
        if (!Assist.Enabled || !(Assist.Strength > 0) || !Feedback || Feedback.Speed <= 3
            || Feedback.Contacts < 2 || Math.abs(this.Steer) > 0.99) this.Correction = 0;
        return { Throttle: this.Throttle, Brake: this.Brake,
            Steer: Math.max(-1, Math.min(1, this.Steer + this.Correction)), Handbrake: Keys.has("Space") };
    }

    Reset()
    {
        this.Throttle = 0.0;
        this.Brake    = 0.0;
        this.Steer    = 0.0;
        this.Correction = 0;
    }
}

// Positive lateral slip: steer toward the travel direction; oppose excess yaw.
// Deadband avoids straight-line hunting. Full manual lock always wins.
export function ResolveCountersteer(Feedback, Manual, { Enabled = false, Strength = 0.45 } = {}) {
    if (!Enabled || !Feedback || Feedback.Speed <= 3 || Feedback.Contacts < 2) return 0;
    const SpeedBlend = Math.min(1, (Feedback.Speed - 3) / 9);
    const Slip = Math.atan2(Feedback.LateralSpeed, Math.max(Feedback.Speed, 1));
    if (Math.abs(Slip) < 0.035) return 0;
    const Lock = ChassisSheet.MaxSteerAngleRad / (1 + Math.max(0, Feedback.Speed - 8) / 22);
    const Wanted = (Slip - 0.12 * Feedback.YawRate) / Lock;
    return Math.max(-0.55, Math.min(0.55, Wanted)) * Math.max(0, Math.min(1, Strength))
        * SpeedBlend * (1 - Math.min(1, Math.abs(Manual)));
}

import { NormalizeVector, SubtractVector, CrossVector } from './TransformProjection.js';
const Clamp = (X, Low, High) => Math.max(Low, Math.min(High, X));
export class DrivingCameraMotion {
    constructor() { this.Reset(); }
    Reset() { this.Time = 0; this.Speed = 0; this.Kick = 0; this.PriorVelocity = null; }
    Apply({ Eye, Target, Up, Fov }, Seconds, Velocity, Options = {}) {
        const Dt = Clamp(Number.isFinite(Seconds) ? Seconds : 0, 0, 0.05);
        if (!Options.Active) { this.Reset(); return { Eye, Target, Up, Fov }; }
        const Speed = Math.hypot(...Velocity);
        this.Speed += (Speed - this.Speed) * (1-Math.exp(-4*Dt));
        let Shock = 0;
        if (this.PriorVelocity && Dt > 0) Shock = Math.hypot(...Velocity.map((V,I) => V-this.PriorVelocity[I])) / Dt;
        this.PriorVelocity = [...Velocity];
        this.Kick = Math.max(this.Kick*Math.exp(-10*Dt), Clamp((Shock-12)/150,0,1));
        this.Time += Dt;
        const Amount = Clamp(this.Speed/45,0,1);
        const Widen = Options.SpeedFov === false ? 0 : Clamp(Options.FovBoost ?? 14,0,24) * Amount*Amount*(3-2*Amount);
        const Strength = Options.Shake === false ? 0 : Clamp(Options.Strength ?? 0.5,0,1);
        const Amplitude = Strength*(0.008*Amount + 0.022*this.Kick);
        const T = this.Time;
        const Noise = (F, Phase) => 0.65*Math.sin(T*F+Phase) + 0.35*Math.sin(T*F*1.73+Phase*2);
        const Forward = NormalizeVector(SubtractVector(Target,Eye));
        const Right = NormalizeVector(CrossVector(Forward,Up));
        const Offset = Right.map((V,I) => Amplitude*(V*Noise(29,1)+Up[I]*Noise(37,2)));
        const Aim = Right.map((V,I) => Amplitude*0.5*(V*Noise(23,3)+Up[I]*Noise(31,4)));
        return { Eye: Eye.map((V,I) => V+Offset[I]), Target: Target.map((V,I) => V+Offset[I]+Aim[I]),
            Up, Fov: Clamp(Fov+Widen,25,105) };
    }
}

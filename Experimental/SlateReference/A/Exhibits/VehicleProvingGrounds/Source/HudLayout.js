// CSS HUD layout only. Deliberately has no dependency on camera/cabin/vehicle transforms.
export const HudDefaults = Object.freeze({ HudScale: 1, HudAcross: 0, HudDown: 0 });
export function ResolveHudStyle(Settings) {
    const Clamp = (Value, Min, Max, Fallback) => Number.isFinite(Value) ? Math.max(Min, Math.min(Max, Value)) : Fallback;
    const Scale = Clamp(Settings.HudScale, 0.6, 1.8, 1);
    const X = Clamp(Settings.HudAcross, -200, 200, 0), Y = Clamp(Settings.HudDown, -200, 200, 0);
    return { transformOrigin: 'top left', transform: `translate(${X}px, ${Y}px) scale(${Scale})` };
}

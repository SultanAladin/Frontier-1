import { test, expect } from "@playwright/test";
import { EvaluateCameraLagWeight } from "../src/CameraNavigation.js";

// 📝 Compare equal elapsed time, not equal numbers of frames.
test("camera lag is bounded and frame-rate independent", () =>
{
    for (const Frequency of [5, 30, 60, 144])
    {
        let Remaining = 1;
        for (let Sample = 0; Sample < Frequency; ++Sample)
            Remaining *= 1 - EvaluateCameraLagWeight(1 / Frequency, 0.12);
        expect(Remaining).toBeCloseTo(Math.exp(-1 / 0.12), 12);
    }
    expect(EvaluateCameraLagWeight(1 / 60, 0)).toBe(1);
    expect(EvaluateCameraLagWeight(-1, 0.12)).toBe(0);
    expect(EvaluateCameraLagWeight(NaN, 0.12)).toBe(0);
});

async function InitializeNavigation(Page)
{
    await Page.goto("/src/CameraNavigation.js");
    await Page.evaluate(async () =>
    {
        const Source = await (await fetch("/src/CameraNavigation.js")).text();
        const ThreeUrl = Source.match(/from\s*"([^"]+three[^"\n]+)"/)[1];
        const { PerspectiveCamera } = await import(ThreeUrl);
        const { CameraNavigation } = await import("/src/CameraNavigation.js");
        document.body.innerHTML = '<canvas width="400" height="300" style="width:400px;height:300px"></canvas>';
        const Camera = new PerspectiveCamera(40, 4 / 3, 0.01, 100);
        Camera.position.set(0, 0, 6);
        const Navigation = new CameraNavigation(Camera, document.querySelector("canvas"));
        Navigation.ConfigureZoomDelta = (Displacement) =>
        {
            Navigation.NavigationCamera.zoom = Math.min(10, Math.max(0.1,
                Navigation.NavigationCamera.zoom * Math.exp(-Displacement * 0.006)));
        };
        Navigation.ConfigureFrame = () =>
        {
            Navigation.NavigationCamera.position.set(0, 0, 6);
            Navigation.NavigationCamera.zoom = 1;
            Navigation.Controls.target.set(0, 0, 0);
            Navigation.Controls.update();
            Navigation.Reset();
        };
        window.NavigationAcceptance = Navigation;
        Navigation.Reset();
    });
}

async function QueryPose(Page)
{
    return Page.evaluate(() =>
    {
        const Navigation = window.NavigationAcceptance;
        return {
            Desired: Navigation.NavigationCamera.position.toArray(),
            Visible: Navigation.ProjectionCamera.position.toArray(),
            Target: Navigation.Controls.target.toArray(),
            Zoom: Navigation.NavigationCamera.zoom,
            VisibleZoom: Navigation.ProjectionCamera.zoom,
        };
    });
}

async function Drag(Page, Modifiers = [], Button = "middle")
{
    for (const Modifier of Modifiers) await Page.keyboard.down(Modifier);
    await Page.mouse.move(180, 140);
    await Page.mouse.down({ button: Button });
    await Page.mouse.move(245, 175, { steps: 5 });
    await Page.mouse.up({ button: Button });
    for (const Modifier of Modifiers) await Page.keyboard.up(Modifier);
}

test("Blender gestures, convergence, release, framing and painting suspension use real pointers", async ({ page: Page }) =>
{
    await InitializeNavigation(Page);
    const Initial = await QueryPose(Page);
    await Drag(Page, [], "left");
    expect(await QueryPose(Page)).toEqual(Initial);
    await Drag(Page);
    const Orbit = await QueryPose(Page);
    expect(Orbit.Desired).not.toEqual(Initial.Desired);
    expect(Orbit.Visible).toEqual(Initial.Visible);
    expect(Orbit.Target).toEqual(Initial.Target);
    await Page.evaluate(() => window.NavigationAcceptance.Evaluate(1 / 60));
    const Lag = await QueryPose(Page);
    expect(Lag.Visible).not.toEqual(Orbit.Visible);
    expect(Lag.Visible).not.toEqual(Orbit.Desired);
    await Page.evaluate(() =>
    {
        for (let Sample = 0; Sample < 240; ++Sample) window.NavigationAcceptance.Evaluate(1 / 60);
    });
    const Settled = await QueryPose(Page);
    for (let Axis = 0; Axis < 3; ++Axis) expect(Settled.Visible[Axis]).toBeCloseTo(Settled.Desired[Axis], 6);
    await Drag(Page, ["Shift"]);
    const Pan = await QueryPose(Page);
    expect(Pan.Target).not.toEqual(Initial.Target);
    await Drag(Page, ["Control"]);
    const Zoom = await QueryPose(Page);
    expect(Zoom.Zoom).not.toBe(Pan.Zoom);
    expect(Zoom.Target).toEqual(Pan.Target);
    expect(Zoom.Desired).toEqual(Pan.Desired);
    await Page.mouse.move(360, 250);
    expect((await QueryPose(Page)).Zoom).toBe(Zoom.Zoom);
    await Page.locator("canvas").focus();
    await Page.keyboard.press("NumpadDecimal");
    expect(await QueryPose(Page)).toEqual(Initial);
    await Drag(Page);
    await Page.evaluate(() => window.NavigationAcceptance.Suspend());
    expect(await QueryPose(Page)).toEqual(Initial);
    await Page.evaluate(() => window.NavigationAcceptance.Finalize());
});

test("spherical orbit, pan and lens convergence agree at 5, 30, 60 and 144 Hz", async ({ page: Page }) =>
{
    await InitializeNavigation(Page);
    const Samples = await Page.evaluate(() =>
    {
        const Navigation = window.NavigationAcceptance;
        return [5, 30, 60, 144].map((Frequency) =>
        {
            Navigation.ConfigureFrame();
            Navigation.Controls.target.set(1, 0.4, 0);
            Navigation.NavigationCamera.position.set(4, 2, 4);
            Navigation.NavigationCamera.zoom = 3;
            Navigation.Controls.update();
            for (let Sample = 0; Sample < Frequency; ++Sample) Navigation.Evaluate(1 / Frequency);
            return [...Navigation.ProjectionCamera.position.toArray(), Navigation.ProjectionCamera.zoom];
        });
    });
    for (const Sample of Samples.slice(1))
        for (let Axis = 0; Axis < Sample.length; ++Axis) expect(Sample[Axis]).toBeCloseTo(Samples[0][Axis], 8);
});


test("paused previews settle without restarting auto-rotation and reduced motion disables lag", async ({ page: Page }) =>
{
    await Page.emulateMedia({ reducedMotion: "reduce" });
    await InitializeNavigation(Page);
    expect(await Page.locator("canvas").getAttribute("data-camera-lag")).toBe("0");
    await Drag(Page);
    const Orbit = await QueryPose(Page);
    await Page.evaluate(() =>
    {
        const Navigation = window.NavigationAcceptance;
        Navigation.Controls.autoRotate = true;
        Navigation.Evaluate(1 / 60, false);
    });
    const Visible = await QueryPose(Page);
    expect(Visible.Desired).toEqual(Orbit.Desired);
    for (let Axis = 0; Axis < 3; ++Axis) expect(Visible.Visible[Axis]).toBeCloseTo(Visible.Desired[Axis], 6);
    expect(await Page.evaluate(() => window.NavigationAcceptance.QueryPending())).toBe(false);
});

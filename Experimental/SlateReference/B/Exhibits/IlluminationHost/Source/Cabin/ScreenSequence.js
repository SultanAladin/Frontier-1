import { CourseMaterial } from "../MaterialSpecification.js";
import { PaintColours } from "../VehicleAppearance.js";
//============================================================================================================================================
//                                                            SCREENSEQUENCE.JS
//============================================================================================================================================
// 📦 Flat device artwork, retained hit regions, launcher variations and local demonstration applications.

// 📝 Copied from Experimental/TabletPanel (arena/215a6f39-frontier) into the IlluminationHost cabin. Three changes, all
//    marked "Cabin:": the Garage preview may be External (the host renders the car into a GPU texture and the cabin
//    overlay composites it into the preview rectangle, so nothing is read back), and the Telemetry and Tyres
//    applications read Live — the car's real speed, tread temperatures and pressures — when the host provides it.

import { Applications, LauncherDesigns, ScreenWidth, ScreenHeight, ResolveApplication, ResolveDesign } from "./AppSpecification.js";

const FontFamily = '"DM Sans", Arial, sans-serif';

export class ScreenSequence
{
    constructor(Canvas, Icons, Announce)
    {
        this.Canvas = Canvas;
        this.Preview = null;
        this.Context = Canvas.getContext("2d", { alpha: false });
        this.Icons = Icons;
        this.Announce = Announce;
        this.Design = "launchpad";
        this.Application = null;
        this.Regions = [];
        this.Hovered = "";
        this.Pressed = "";
        this.Pointer = null;
        this.FocusIndex = 0;
        this.Dirty = true;
        this.Revision = 0;
        this.Values = { steering: 50, height: 35, stiffness: 65, spoiler: 14, pressure: 185, temperature: 22,
            fan: 2, volume: 55, brightness: 100, laps: 8, zoom: 1 };
        this.VehicleTab = "Steering";
        this.RoomReady = false;
        this.Invited = "";
        this.MediaPlaying = false;
        this.Nickname = "Aladin";
        this.MessageDraft = "";
        this.Messages = [{ From: "Amelia", Text: "Same road. New adventure?" }, { From: "You", Text: "See you in the Drive Room." }];
        this.TextTarget = "";
        this.FinishColour = "#a4d8be";
        this.Notice = "";
        this.Clock = "";
        this.ActiveTextInput = null;
        this.Live = null;   // Cabin: { Speed [km/h], Temperatures [°C ×4], Pressures [kPa ×4], Tread [0–1 ×4], States [string ×4] }
        this.Scale = 1.6;
        this.OffsetAcross = 0;
        this.OffsetDown = 0;
        this.PanelWidth = ScreenWidth;
        this.PanelHeight = ScreenHeight;
        this.PresentAspect(ScreenWidth / ScreenHeight);
    }

    // 📝 Every car's panel has its own proportions; the launcher keeps its 16:10 layout at uniform scale and the panel
    //    background continues into the spare band, so one UI serves every console without stretching.
    PresentAspect(Aspect)
    {
        const Reference = ScreenWidth / ScreenHeight;
        this.PanelWidth = Aspect >= Reference ? ScreenHeight * Aspect : ScreenWidth;
        this.PanelHeight = Aspect >= Reference ? ScreenHeight : ScreenWidth / Aspect;
        this.OffsetAcross = (this.PanelWidth - ScreenWidth) / 2;
        this.OffsetDown = (this.PanelHeight - ScreenHeight) / 2;
        this.Canvas.width = Math.round(this.PanelWidth * this.Scale);
        this.Canvas.height = Math.round(this.PanelHeight * this.Scale);
        this.Dirty = true;
    }

    QueryPanelPoint(FractionAcross, FractionDown)
    {
        return { Across: FractionAcross * this.PanelWidth - this.OffsetAcross, Down: FractionDown * this.PanelHeight - this.OffsetDown };
    }

    QueryPanelFraction(Across, Down)
    {
        return { Across: (Across + this.OffsetAcross) / this.PanelWidth, Down: (Down + this.OffsetDown) / this.PanelHeight };
    }

    PresentDesign(Identifier)
    {
        if (!ResolveDesign(Identifier)) return false;
        this.Design = Identifier;
        this.PresentHome();
        return true;
    }

    PresentHome()
    {
        this.Application = null;
        this.Notice = "";
        this.Pointer = null;
        this.Hovered = "";
        this.Pressed = "";
        this.TextTarget = "";
        this.ActiveTextInput?.blur();
        this.Dirty = true;
        this.Announce?.();
    }

    PresentApplication(Identifier)
    {
        if (!ResolveApplication(Identifier)) return false;
        this.Application = Identifier;
        this.Notice = "";
        this.Hovered = "";
        this.Pressed = "";
        this.TextTarget = "";
        this.ActiveTextInput?.blur();
        this.Dirty = true;
        this.Announce?.();
        return true;
    }

    AdvanceClock()
    {
        const Reading = new Date().toLocaleTimeString("en-GB", { hour: "2-digit", minute: "2-digit" });
        if (Reading !== this.Clock) { this.Clock = Reading; this.Dirty = true; }
    }

    RasterRectangle(Left, Top, Width, Height, Radius, Colour, Stroke = null)
    {
        const Context = this.Context;
        Context.beginPath();
        Context.roundRect(Left, Top, Width, Height, Radius);
        Context.fillStyle = Colour;
        Context.fill();
        if (Stroke) { Context.strokeStyle = Stroke; Context.lineWidth = 1; Context.stroke(); }
    }

    RasterText(Text, Left, Top, Size = 20, Colour = "#e8eeee", Align = "left", Weight = 400)
    {
        const Context = this.Context;
        Context.font = `${Weight} ${Size}px ${FontFamily}`;
        Context.textBaseline = "alphabetic";
        Context.textAlign = Align;
        Context.fillStyle = Colour;
        Context.fillText(String(Text), Left, Top);
    }

    RasterIcon(Identifier, Left, Top, Extent)
    {
        this.Context.drawImage(this.Icons[Identifier], Left, Top, Extent, Extent);
    }

    RasterRule(Left, Top, Width, Colour = "#ffffff15")
    {
        this.Context.fillStyle = Colour;
        this.Context.fillRect(Left, Top, Width, 1);
    }

    AdmitRegion(Identifier, Left, Top, Width, Height, Action, Extra = {})
    {
        this.Regions.push({ Identifier, Left, Top, Width, Height, Action, ...Extra });
    }

    RasterButton(Identifier, Text, Left, Top, Width, Height, Action, Colour = "#253939", Ink = "#d7e7df")
    {
        const Hover = this.Hovered === Identifier;
        this.RasterRectangle(Left, Top, Width, Height, 14, Hover ? "#466057" : Colour);
        this.RasterText(Text, Left + Width / 2, Top + Height / 2 + 7, 19, Ink, "center");
        this.AdmitRegion(Identifier, Left, Top, Width, Height, Action);
    }

    RasterBackground(Light = false)
    {
        const Context = this.Context;
        const Colour = Context.createLinearGradient(0, 0, ScreenWidth, ScreenHeight);
        Colour.addColorStop(0, Light ? "#f1f1e7" : "#17262c");
        Colour.addColorStop(1, Light ? "#dfe8db" : "#121a22");
        Context.fillStyle = Colour;
        Context.fillRect(-this.OffsetAcross, -this.OffsetDown, this.PanelWidth, this.PanelHeight);
        const Glow = Context.createRadialGradient(1010, 70, 20, 960, 50, 670);
        Glow.addColorStop(0, Light ? "#ffffff85" : "#8eb29b26");
        Glow.addColorStop(1, "#8eb29b00");
        Context.fillStyle = Glow;
        Context.fillRect(-this.OffsetAcross, -this.OffsetDown, this.PanelWidth, this.PanelHeight);
    }

    RasterStatus(Light = false)
    {
        const Ink = Light ? "#394a46" : "#dae8e3";
        const Muted = Light ? "#86948a" : "#82978e";
        this.RasterText("F R O N T I E R", 48, 49, 15, Ink);
        this.RasterText("P", 242, 49, 16, Muted);
        this.RasterText("0", 269, 49, 24, Ink);
        this.RasterText("km/h", 290, 49, 12, Muted);
        this.Context.beginPath();
        this.Context.arc(941, 42, 3, 0, Math.PI * 2);
        this.Context.fillStyle = "#8dac8c";
        this.Context.fill();
        this.RasterText("LOCAL DEMO", 953, 48, 11, Muted);
        this.RasterText(this.Clock, 1150, 49, 17, Ink, "right");
        this.RasterRule(48, 74, 1104, Light ? "#283c2812" : "#ffffff0b");
    }

    RasterFooter(Light = false)
    {
        const Ink = Light ? "#5b6d5f" : "#879d92";
        this.RasterText("CONTROLVEHICLE", 50, 708, 12, Ink);
        this.RasterText("22°", 1096, 708, 19, Ink);
        this.RasterText("AUTO", 1129, 708, 10, Ink);
        this.RasterRectangle(553, 697, 94, 5, 3, Light ? "#8a9a8655" : "#e2eadd50");
        this.AdmitRegion("home", 505, 678, 190, 50, () => this.PresentHome());
        this.AdmitRegion("climate-shortcut", 1070, 678, 88, 50, () => this.PresentApplication("climate"));
    }

    RasterApplicationTile(Application, Left, Top, Width, Height, Style = "icon", Light = false)
    {
        const Identifier = "app:" + Application.Identifier;
        const Hover = this.Hovered === Identifier;
        const Pressed = this.Pressed === Identifier;
        const Ink = Light ? "#33453d" : "#e4ebe5";
        if (Style === "icon")
        {
            if (Hover || Pressed) this.RasterRectangle(Left + 8, Top, Width - 16, Height, 22, Light ? "#ffffff90" : "#ffffff0b");
            this.RasterIcon(Application.Identifier, Left + (Width - 85) / 2, Top + 17, 85);
            this.RasterText(Application.Name, Left + Width / 2, Top + 129, 21, Ink, "center");
            if (Application.Badge) this.RasterBadge(Application.Badge, Left + Width / 2 + 42, Top + 13);
        }
        else if (Style === "row")
        {
            this.RasterRectangle(Left, Top, Width, Height, 18, Hover ? "#41574c" : "#ffffff09");
            this.RasterIcon(Application.Identifier, Left + 16, Top + (Height - 55) / 2, 55);
            this.RasterText(Application.Name, Left + 86, Top + 37, 20, Ink);
            this.RasterText(Application.Caption, Left + 86, Top + 60, 12, Light ? "#7d8e79" : "#879b90");
            if (Application.Badge) this.RasterBadge(Application.Badge, Left + Width - 20, Top + 17);
        }
        else
        {
            const Colour = this.Context.createLinearGradient(Left, Top, Left + Width, Top + Height);
            Colour.addColorStop(0, Hover ? "#3c5548" : "#ffffff0d");
            Colour.addColorStop(1, "#ffffff04");
            this.RasterRectangle(Left, Top, Width, Height, 24, Colour, "#ffffff0a");
            this.RasterIcon(Application.Identifier, Left + 21, Top + 20, 58);
            this.RasterText(Application.Name, Left + 22, Top + 108, 23, Ink);
            if (Height > 145) this.RasterText(Application.Caption, Left + 22, Top + 133, 12, "#91a496");
            if (Application.Badge) this.RasterBadge(Application.Badge, Left + Width - 23, Top + 22);
        }
        this.AdmitRegion(Identifier, Left, Top, Width, Height, () => this.PresentApplication(Application.Identifier));
    }

    RasterBadge(Text, CentreX, CentreY)
    {
        this.RasterRectangle(CentreX - 12, CentreY - 10, 24, 20, 10, "#d3b3a3");
        this.RasterText(Text, CentreX, CentreY + 4, 12, "#3e3633", "center");
    }

    RasterLaunchpad()
    {
        this.RasterBackground();
        this.RasterStatus();
        this.RasterText("Welcome back, " + this.Nickname + ".", 56, 158, 41, "#ebefdf", "left", 300);
        this.RasterText("Your car. Your people. Your space.", 57, 197, 17, "#82968d");
        this.RasterRectangle(986, 120, 155, 68, 19, "#b1c5a115");
        this.RasterText("ALL APPS", 1008, 146, 10, "#9faf98");
        this.RasterText("12", 1008, 176, 25, "#d3dec7");
        for (let Index = 0; Index < Applications.length; ++Index)
        {
            const Column = Index % 6;
            const Row = Math.floor(Index / 6);
            this.RasterApplicationTile(Applications[Index], 40 + Column * 188, 255 + Row * 181, 176, 149);
        }
        this.RasterFooter();
    }

    RasterDashboard()
    {
        this.RasterBackground();
        this.RasterStatus();
        this.RasterText("A good day to drive.", 50, 151, 40, "#e4eddc", "left", 300);
        this.RasterText("Your essentials, brought together.", 52, 184, 16, "#83988a");
        const Hero = this.Context.createLinearGradient(50, 218, 385, 600);
        Hero.addColorStop(0, "#bbd2b0");
        Hero.addColorStop(1, "#86a698");
        this.RasterRectangle(50, 220, 340, 392, 29, Hero);
        this.RasterText("YOUR VEHICLE", 78, 264, 12, "#557362");
        this.RasterIcon("vehicle", 123, 295, 192);
        this.RasterText("ControlVehicle", 78, 531, 33, "#253e33", "left", 300);
        this.RasterText("Ready when you are.", 79, 562, 15, "#577867");
        this.RasterText("Explore  →", 79, 589, 15, "#304f3c");
        this.AdmitRegion("app:vehicle", 50, 220, 340, 392, () => this.PresentApplication("vehicle"));
        const Cards = Applications.filter(Application => Application.Identifier !== "vehicle" && !["account", "settings"].includes(Application.Identifier));
        for (let Index = 0; Index < Cards.length; ++Index)
            this.RasterApplicationTile(Cards[Index], 411 + (Index % 3) * 246, 220 + Math.floor(Index / 3) * 132, 230, 116, "card");
        this.RasterIcon("account", 54, 631, 28);
        this.RasterText(this.Nickname, 94, 653, 15, "#95aa97");
        this.AdmitRegion("app:account", 50, 623, 140, 40, () => this.PresentApplication("account"));
        this.RasterIcon("settings", 1010, 630, 27);
        this.RasterText("Settings", 1050, 651, 15, "#95aa97");
        this.AdmitRegion("app:settings", 997, 623, 148, 40, () => this.PresentApplication("settings"));
        this.RasterFooter();
    }

    RasterCollections()
    {
        this.RasterBackground(true);
        this.RasterStatus(true);
        this.RasterText("Everything in its place.", 53, 154, 40, "#2f473b", "left", 300);
        this.RasterText("A little less searching. A little more driving.", 55, 191, 16, "#82947f");
        const Categories = ["Vehicle", "Connect", "Personal"];
        const Colours = ["#d5e5d4", "#e0dfcf", "#d8e0e5"];
        for (let Column = 0; Column < Categories.length; ++Column)
        {
            const Left = 49 + Column * 375;
            const Category = Categories[Column];
            this.RasterRectangle(Left, 234, 351, 414, 29, Colours[Column]);
            this.RasterText(Category, Left + 27, 280, 26, "#3c5443");
            this.RasterText("0" + (Column + 1), Left + 318, 279, 13, "#94a28d", "right");
            const Members = Applications.filter(Application => Application.Category === Category);
            for (let Index = 0; Index < Members.length; ++Index)
            {
                const Application = Members[Index];
                const Top = 305 + Index * 80;
                const Hover = this.Hovered === "app:" + Application.Identifier;
                this.RasterRectangle(Left + 13, Top, 325, 70, 15, Hover ? "#ffffff98" : "#ffffff40");
                this.RasterIcon(Application.Identifier, Left + 25, Top + 11, 48);
                this.RasterText(Application.Name, Left + 91, Top + 39, 20, "#425540");
                this.RasterText("›", Left + 312, Top + 39, 24, "#8b9e82", "right");
                this.AdmitRegion("app:" + Application.Identifier, Left + 13, Top, 325, 70, () => this.PresentApplication(Application.Identifier));
            }
        }
        this.RasterFooter(true);
    }

    RasterFocus()
    {
        this.RasterBackground();
        this.RasterStatus();
        this.RasterText("Make room for what matters.", 53, 144, 37, "#e3eddf", "left", 300);
        this.RasterText("Swipe to explore. Tap to step inside.", 55, 179, 15, "#859b8e");
        for (const Offset of [-1, 0, 1])
        {
            const Index = (this.FocusIndex + Offset + Applications.length) % Applications.length;
            const Application = Applications[Index];
            const Centre = 600 + Offset * 406;
            const Main = Offset === 0;
            const Width = Main ? 375 : 286;
            const Top = Main ? 220 : 252;
            const Height = Main ? 342 : 278;
            const Colour = this.Context.createLinearGradient(Centre, Top, Centre, Top + Height);
            Colour.addColorStop(0, Main ? "#d7dfc6" : "#253936");
            Colour.addColorStop(1, Main ? "#a3bba3" : "#1d2c2c");
            this.RasterRectangle(Centre - Width / 2, Top, Width, Height, 27, Colour);
            this.RasterIcon(Application.Identifier, Centre - (Main ? 55 : 37), Top + 38, Main ? 110 : 74);
            this.RasterText(Application.Name, Centre, Top + (Main ? 201 : 158), Main ? 32 : 24, Main ? "#314b39" : "#c0d4c2", "center", 300);
            this.RasterText(Application.Caption, Centre, Top + (Main ? 232 : 185), 13, Main ? "#698269" : "#7d9783", "center");
            this.RasterText(Main ? "Open application  →" : "Tap to focus", Centre, Top + Height - 32, Main ? 17 : 13, Main ? "#466749" : "#87a18a", "center");
            this.AdmitRegion(Main ? "app:" + Application.Identifier : "focus:" + Index, Centre - Width / 2, Top, Width, Height,
                () => { if (Main) this.PresentApplication(Application.Identifier); else { this.FocusIndex = Index; this.Dirty = true; } }, { Swipe: true });
        }
        for (let Index = 0; Index < Applications.length; ++Index)
        {
            const Left = 176 + Index * 71;
            const Selected = Index === this.FocusIndex;
            if (Selected) this.RasterRectangle(Left - 9, 598, 57, 57, 16, "#b8d5a320");
            this.RasterIcon(Applications[Index].Identifier, Left, 607, 39);
            this.AdmitRegion("focus-choice:" + Applications[Index].Identifier, Left - 9, 598, 57, 57,
                () => { this.FocusIndex = Index; this.Dirty = true; });
        }
        this.RasterFooter();
    }

    RasterConsole()
    {
        this.RasterBackground();
        this.RasterRectangle(-this.OffsetAcross, -this.OffsetDown, 250 + this.OffsetAcross, this.PanelHeight, 0, "#edf0e3");
        this.RasterText("F R O N T I E R", 34, 51, 13, "#62745f");
        this.RasterText("0", 125, 184, 93, "#344739", "center", 300);
        this.RasterText("km/h", 125, 216, 15, "#8c9b80", "center");
        this.RasterRectangle(75, 244, 100, 31, 15, "#dce5d2");
        this.RasterText("P  ·  PARKED", 125, 264, 11, "#6e8362", "center");
        this.RasterIcon("vehicle", 65, 351, 120);
        this.RasterText("ControlVehicle", 125, 517, 20, "#465e42", "center");
        this.RasterText("Welcome, " + this.Nickname, 125, 549, 13, "#899b7e", "center");
        this.RasterRule(42, 611, 166, "#617a4b22");
        this.RasterText("LOCAL", 125, 656, 10, "#82986f", "center");
        this.RasterText("YOUR APPS", 290, 52, 12, "#8a9d89");
        this.RasterText(this.Clock, 1151, 52, 17, "#a9bba4", "right");
        this.RasterText("Choose your next move.", 290, 143, 35, "#e0ebd6", "left", 300);
        this.RasterText("Simple controls. Clear choices.", 292, 177, 15, "#849b80");
        for (let Index = 0; Index < Applications.length; ++Index)
            this.RasterApplicationTile(Applications[Index], 286 + (Index % 3) * 293, 218 + Math.floor(Index / 3) * 111, 276, 94, "row");
        this.RasterRectangle(670, 697, 86, 5, 3, "#dbe8cc40");
        this.AdmitRegion("home", 625, 678, 180, 50, () => this.PresentHome());
    }

    RasterSlider(Identifier, Name, Unit, Left, Top, Width, Minimum, Maximum, Step = 1)
    {
        const Value = this.Values[Identifier];
        this.RasterText(Name, Left, Top, 22, "#d8e4d5");
        this.RasterText(Value + Unit, Left + Width, Top, 24, "#b5d4a8", "right");
        this.RasterRectangle(Left, Top + 29, Width, 5, 3, "#3b4b42");
        const Fraction = (Value - Minimum) / (Maximum - Minimum);
        this.RasterRectangle(Left, Top + 29, Math.max(2, Width * Fraction), 5, 3, "#a3c394");
        this.Context.beginPath();
        this.Context.arc(Left + Width * Fraction, Top + 31, 10, 0, Math.PI * 2);
        this.Context.fillStyle = "#dce9ce";
        this.Context.fill();
        this.AdmitRegion("slider:" + Identifier, Left - 12, Top + 6, Width + 24, 49, null,
            { Slider: Identifier, TrackLeft: Left, TrackWidth: Width, Minimum, Maximum, Step });
    }

    RasterApplication()
    {
        this.RasterBackground();
        this.RasterStatus();
        const Application = ResolveApplication(this.Application);
        this.RasterButton("back", "‹  Apps", 48, 103, 107, 48, () => this.PresentHome(), "#ffffff0b");
        this.RasterIcon(Application.Identifier, 190, 97, 65);
        this.RasterText(Application.Name, 273, 136, 35, "#e3eddd", "left", 300);
        this.RasterText(Application.Caption, 274, 164, 15, "#879e89");
        this.RasterText("LOCAL DEMONSTRATION", 1140, 133, 10, "#8ea18c", "right");
        this.RasterRectangle(48, 209, 1104, 430, 28, "#ffffff06", "#ffffff0b");
        this.RasterApplicationContent(Application.Identifier);
        this.RasterText(this.Notice || (Application.Identifier === "garage" ? "Live vehicle materials · drag a swatch onto the body, or rim material onto a rim" : "Design prototype · online services are not connected"), 64, 665, 12, "#708875");
        this.RasterFooter();
    }

    RasterApplicationContent(Identifier)
    {
        if (Identifier === "vehicle")
        {
            this.RasterText("VEHICLE SETUP", 79, 256, 12, "#8fa487");
            for (const [Index, Tab] of ["Steering", "Suspension", "Aerodynamics"].entries())
                this.RasterButton("tab:" + Tab, Tab, 80 + Index * 245, 279, 229, 50,
                    () => { this.VehicleTab = Tab; this.Dirty = true; }, this.VehicleTab === Tab ? "#a6c396" : "#243931", this.VehicleTab === Tab ? "#304734" : "#acbba8");
            if (this.VehicleTab === "Steering")
            {
                this.RasterSlider("steering", "Mouse steering sensitivity", "%", 93, 410, 686, 10, 100);
                this.RasterText("Fine adjustment for your next drive.", 93, 478, 17, "#859b80");
            }
            else if (this.VehicleTab === "Suspension")
            {
                this.RasterSlider("height", "Ride height", " mm", 93, 389, 686, 15, 70);
                this.RasterSlider("stiffness", "Spring stiffness", "%", 93, 497, 686, 20, 100);
            }
            else this.RasterSlider("spoiler", "Rear wing angle", "°", 93, 409, 686, 4, 22);
            this.RasterIcon("vehicle", 891, 374, 135);
            this.RasterButton("vehicle-reset", "Restore defaults", 91, 555, 205, 49,
                () => { Object.assign(this.Values, { steering: 50, height: 35, stiffness: 65, spoiler: 14 }); this.Dirty = true; });
        }
        else if (Identifier === "drive-room")
        {
            this.RasterText("THE EVENING DRIVE", 82, 265, 14, "#91a286");
            this.RasterText("Your room. Your rules.", 80, 313, 34, "#d9e6ca", "left", 300);
            for (const [Index, Name] of ["Aladin", "Amelia", "Noah"].entries())
            {
                this.RasterRectangle(80, 345 + Index * 72, 527, 58, 16, "#b7d0980b");
                this.RasterText(Name, 107, 382 + Index * 72, 20, "#c2d8b6");
                this.RasterText(Index === 0 ? "YOU" : "DEMO CONTACT", 583, 382 + Index * 72, 10, "#809b6d", "right");
            }
            this.RasterText("Highlands Loop", 688, 317, 26, "#cfdec3");
            this.RasterSlider("laps", "Laps", "", 688, 399, 379, 1, 20);
            this.RasterButton("ready", this.RoomReady ? "Ready" : "Mark as ready", 688, 510, 379, 64,
                () => { this.RoomReady = !this.RoomReady; this.Notice = "Local readiness updated. No network room is joined."; this.Dirty = true; }, this.RoomReady ? "#b6ca99" : "#33513c", this.RoomReady ? "#385032" : "#c4d8b4");
        }
        else if (Identifier === "messages")
        {
            this.RasterText("AMELIA", 85, 265, 15, "#9db6a1");
            const Recent = this.Messages.slice(-4);
            for (let Index = 0; Index < Recent.length; ++Index)
            {
                const Message = Recent[Index];
                const Own = Message.From === "You";
                const Left = Own ? 419 : 85;
                this.RasterRectangle(Left, 286 + Index * 65, 650, 52, 15, Own ? "#abc1a726" : "#edf8ef0a");
                this.RasterText(Message.Text.slice(0, 62), Left + 19, 319 + Index * 65, 18, Own ? "#cee6bd" : "#c1d8c8");
            }
            this.RasterRectangle(83, 575, 800, 44, 12, "#12221c");
            this.RasterText(this.MessageDraft || "Write a message…", 102, 604, 18, this.MessageDraft ? "#d9ead0" : "#78917b");
            this.AdmitRegion("message-input", 83, 575, 800, 44, () => this.ReceiveText("message"));
            this.RasterButton("send-message", "Send", 904, 575, 181, 44, () => this.DeliverMessage(), "#a6c795", "#34512e");
        }
        else if (Identifier === "contacts")
        {
            for (const [Index, Name] of ["Amelia", "Noah", "Zara"].entries())
            {
                const Top = 269 + Index * 104;
                this.RasterIcon("contacts", 86, Top, 66);
                this.RasterText(Name, 174, Top + 30, 24, "#d9e4cc");
                this.RasterText("Demo contact", 175, Top + 58, 14, "#7d997d");
                this.RasterButton("invite:" + Name, this.Invited === Name ? "Invited" : "Invite to room", 830, Top + 5, 255, 55,
                    () => { this.Invited = Name; this.Notice = Name + " invited in the local prototype only."; this.Dirty = true; });
            }
        }
        else if (Identifier === "account")
        {
            this.RasterIcon("account", 85, 277, 134);
            this.RasterText("YOUR PROFILE", 271, 281, 12, "#8d9d8b");
            this.RasterText(this.Nickname, 268, 331, 40, "#d6e8c9", "left", 300);
            this.RasterText("A local profile for this design study.", 270, 367, 18, "#7d9679");
            this.RasterButton("rename", "Edit display name", 269, 407, 270, 55, () => this.ReceiveText("nickname"));
            this.RasterText("Display name", 269, 526, 17, "#85997d");
            this.RasterRectangle(267, 546, 560, 47, 12, "#12221c");
            this.RasterText(this.Nickname, 287, 577, 20, "#d1e7c2");
            this.AdmitRegion("nickname-input", 267, 546, 560, 47, () => this.ReceiveText("nickname"));
            this.RasterButton("save-name", "Done", 850, 546, 236, 47, () => { this.ActiveTextInput?.blur(); this.TextTarget = ""; this.Notice = "Display name saved locally for this visit."; this.Dirty = true; });
        }
        else if (Identifier === "climate")
        {
            this.RasterText("SET YOUR COMFORT", 86, 263, 13, "#8ca68a");
            this.RasterText(this.Values.temperature + "°", 354, 418, 114, "#dcebcf", "center", 300);
            this.RasterButton("colder", "−", 144, 458, 88, 62, () => this.IncrementValue("temperature", -1, 16, 29));
            this.RasterButton("warmer", "+", 476, 458, 88, 62, () => this.IncrementValue("temperature", 1, 16, 29));
            this.RasterIcon("climate", 847, 285, 120);
            this.RasterSlider("fan", "Fan speed", "", 687, 494, 378, 0, 5);
        }
        else if (Identifier === "tyres")
        {
            this.RasterIcon("tyres", 85, 281, 135);
            this.RasterText("FOUR CORNERS. ONE DRIVE.", 267, 276, 12, "#90a995");
            this.RasterText("Cold pressure", 268, 324, 33, "#dbe7d6", "left", 300);
            this.RasterSlider("pressure", "Selected pressure", " kPa", 269, 420, 752, 140, 240);
            // Cabin: the four corners live — temperature, pressure, tread left and any failure.
            const Live = this.Live;
            if (Live)
            {
                for (const [Index, Name] of ["FL", "FR", "RL", "RR"].entries())
                {
                    const Left = 270 + Index * 196;
                    const State = Live.States[Index];
                    this.RasterText(`${Name} ${Math.round(Live.Temperatures[Index])}°C`, Left, 512, 19, State ? "#e2ada4" : "#94b9aa");
                    this.RasterText(`${Math.round(Live.Pressures[Index])} kPa · ${Math.round(Live.Tread[Index] * 100)}%`, Left, 538, 15, "#88a285");
                    if (State) this.RasterText(State.toUpperCase(), Left, 562, 12, "#e2ada4");
                }
                this.RasterText("Live tyre telemetry · the slider pumps every intact tyre.", 270, 600, 15, "#778f7c");
            }
            else
            {
                this.RasterText("FL 20°C       FR 20°C       RL 20°C       RR 20°C", 270, 523, 19, "#94b9aa");
                this.RasterText("Demonstration values, not live tyre telemetry.", 270, 564, 15, "#778f7c");
            }
        }
        else if (Identifier === "media")
        {
            const Colour = this.Context.createLinearGradient(85, 265, 363, 576);
            Colour.addColorStop(0, "#bbb3ca"); Colour.addColorStop(1, "#436a70");
            this.RasterRectangle(84, 265, 305, 311, 25, Colour);
            this.RasterIcon("media", 174, 336, 125);
            this.RasterText("OPEN ROADS", 417, 308, 12, "#97a98d");
            this.RasterText("A slower kind of rush", 414, 361, 33, "#e0e8d4", "left", 300);
            this.RasterText("Player interface demo · no audio stream", 418, 402, 16, "#7c9580");
            this.RasterButton("media-play", this.MediaPlaying ? "Pause" : "Play", 418, 439, 205, 53,
                () => { this.MediaPlaying = !this.MediaPlaying; this.Dirty = true; }, "#c0c9a7", "#3b5037");
            this.RasterSlider("volume", "Volume", "%", 418, 548, 650, 0, 100);
        }
        else if (Identifier === "navigation")
        {
            const Context = this.Context;
            this.RasterRectangle(78, 240, 730, 370, 19, "#284640");
            Context.save();
            Context.beginPath(); Context.roundRect(78, 240, 730, 370, 19); Context.clip();
            const CentreX = 443, CentreY = 425;
            Context.translate(CentreX, CentreY); Context.scale(this.Values.zoom, this.Values.zoom); Context.translate(-CentreX, -CentreY);
            for (let Index = 0; Index < 7; ++Index)
            {
                Context.beginPath();
                Context.moveTo(38, 264 + Index * 62);
                Context.bezierCurveTo(220, 170 + Index * 71, 630, 325 + Index * 58, 920, 227 + Index * 61);
                Context.strokeStyle = "#cbe1bc11"; Context.lineWidth = 1; Context.stroke();
            }
            Context.beginPath(); Context.moveTo(160, 630); Context.bezierCurveTo(315, 513, 237, 354, 596, 332); Context.bezierCurveTo(671, 328, 694, 272, 797, 217);
            Context.strokeStyle = "#8cccac"; Context.lineWidth = 8; Context.stroke();
            this.RasterIcon("navigation", 470, 360, 55);
            Context.restore();
            this.RasterText("HIGHLANDS LOOP", 838, 293, 12, "#89a38b");
            this.RasterText("Find your road.", 836, 336, 27, "#d3e5c8", "left", 300);
            this.RasterText("Local route illustration", 838, 369, 14, "#7c987c");
            this.RasterButton("zoom-in", "+", 841, 435, 99, 64, () => this.IncrementValue("zoom", 0.25, 0.75, 2));
            this.RasterButton("zoom-out", "−", 958, 435, 99, 64, () => this.IncrementValue("zoom", -0.25, 0.75, 2));
        }
        else if (Identifier === "telemetry")
        {
            this.RasterText("DEMONSTRATION READINGS", 83, 264, 12, "#90a38b");
            // Cabin: live readings when the host provides them.
            const Live = this.Live;
            const Readings = Live ? [["Speed", String(Math.round(Live.Speed)), "km/h"],
                                     ["Hottest tread", String(Math.round(Math.max(...Live.Temperatures))), "°C"],
                                     ["Lowest pressure", String(Math.round(Math.min(...Live.Pressures))), "kPa"]]
                                  : [["Speed", "0", "km/h"], ["Tread", "20", "°C"], ["Pressure", String(this.Values.pressure), "kPa"]];
            for (const [Index, Reading] of Readings.entries())
            {
                const Left = 83 + Index * 340;
                this.RasterRectangle(Left, 300, 316, 249, 21, "#c9d5b407");
                this.RasterText(Reading[0], Left + 24, 342, 18, "#8c9f87");
                this.RasterText(Reading[1], Left + 24, 446, 71, "#d4e4c4", "left", 300);
                this.RasterText(Reading[2], Left + 25, 486, 19, "#88a285");
            }
            this.RasterText(Live ? "Live from the ControlVehicle's solver." : "Native vehicle readings will connect in the C++ phase.", 87, 594, 17, "#8aa380");
        }
        else if (Identifier === "garage")
        {
            const Preview = this.Preview;
            // 📝 The preview target is 640 × 400; draw it 1:1 so the car is never squashed into the page layout.
            if (this.DragMaterial) this.RasterRectangle(74, 244, 648, 408, 24, "#a4d8be");
            this.RasterRectangle(78, 248, 640, 400, 22, "#0d1416");
            if (Preview?.External)
            {
                // Cabin: the host renders the car into a texture the overlay composites here — leave the well dark.
                this.RasterText(Preview.Facets.toLocaleString("en-GB") + " facets · ControlVehicle · rendered live by the exhibit", 96, 630, 13, "#8fa89c");
            }
            else if (Preview?.Loaded)
            {
                const Context = this.Context;
                Context.save();
                Context.beginPath(); Context.roundRect(78, 248, 640, 400, 22); Context.clip();
                // 📝 GL render targets are bottom-up; flip once while compositing so the car sits on its wheels.
                Context.translate(78, 648); Context.scale(1, -1);
                Context.drawImage(Preview.Readback, 0, 0, 640, 400);
                Context.restore();
                this.RasterText(Preview.Facets.toLocaleString("en-GB") + " facets · ControlVehicle · own camera & lighting", 96, 630, 13, "#8fa89c");
            }
            else this.RasterText(Preview?.Failure ? "Preview unavailable: " + Preview.Failure : "Loading your car…", 428, 455, 20, "#7d968b", "center");
            this.AdmitRegion("preview-orbit", 78, 248, 640, 400, null, { Orbit: true });
            this.RasterText("YOUR GARAGE", 762, 291, 12, "#98a98f");
            this.RasterText("ControlVehicle", 760, 349, 39, "#dce8cc", "left", 300);
            this.RasterText("Click a finish or drag paint/rims onto the car.", 763, 392, 17, "#849c81");
            this.RasterText("Apply body paint · live vehicle material", 763, 440, 15, "#c5d9ba");
            for (const [Index, Colour] of PaintColours.entries())
            {
                const Left = 762 + (Index % 2) * 128, Top = 462 + Math.floor(Index / 2) * 74;
                this.RasterRectangle(Left, Top, 110, 58, 16, Colour, this.FinishColour === Colour ? "#e8e8d5" : null);
                this.AdmitRegion("colour:" + Index, Left, Top, 110, 58, () => { this.FinishColour = Colour; this.Appearance?.Apply({ Colour }); this.Notice = "Paint applied to the live vehicle."; this.Dirty = true; }, { Material: { Kind: "paint", Colour } });
            }
            this.RasterButton("finish", this.Appearance?.Matte ? "Matte coat" : "Gloss coat", 762, 606, 115, 36,
                () => { this.Appearance?.Apply({ Matte: !this.Appearance.Matte }); this.Dirty = true; });
            this.RasterButton("wheels", this.Appearance?.DarkWheels ? "Dark rims" : "Alloy rims", 885, 606, 115, 36,
                () => { this.Appearance?.Apply({ DarkWheels: !this.Appearance.DarkWheels, BlueRims: false }); this.Dirty = true; });
            const RimRegion = this.Regions.find(Region => Region.Identifier === "wheels");
            if (RimRegion) RimRegion.Material = { Kind: "wheel", DarkWheels: !this.Appearance?.DarkWheels, BlueRims: false };
            this.RasterButton("blue-rims", "Blue accents", 1008, 606, 128, 36,
                () => { this.Appearance?.Apply({ DarkWheels: false, BlueRims: true }); this.Dirty = true; });
            const BlueRegion = this.Regions.find(R => R.Identifier === "blue-rims");
            if (BlueRegion) BlueRegion.Material = { Kind: "wheel", DarkWheels: false, BlueRims: true };
            const FinishRegion = this.Regions.find(R => R.Identifier === "finish");
            if (FinishRegion) FinishRegion.Material = { Kind: "finish", Matte: !this.Appearance?.Matte };
            this.RasterButton("inspection", ["Solid", "Ghost", "Wireframe", "Scan"][this.Appearance?.View || 0], 762, 650, 238, 36,
                () => { if (this.Appearance) this.Appearance.View = (this.Appearance.View + 1) % 4; this.Dirty = true; });
            this.RasterButton("preview-spin", Preview?.Spin ? "Pause turntable" : "Resume turntable", 762, 694, 238, 36,
                () => { if (Preview) { Preview.Spin = !Preview.Spin; Preview.Dirty = true; this.Dirty = true; } }, "#243931", "#acbba8");
        }
        else
        {
            this.RasterText("MAKE THIS SCREEN YOURS", 84, 264, 12, "#8da086");
            this.RasterSlider("brightness", "Screen brightness", "%", 91, 361, 962, 0, 100);
            this.RasterText("App grid", 93, 452, 21, "#c5d9ba");
            this.RasterText("Pick how your launcher arranges the same twelve apps.", 93, 480, 15, "#84997f");
            for (const [Index, Design] of LauncherDesigns.entries())
            {
                const Left = 89 + Index * 202, Top = 500, Chosen = this.Design === Design.Identifier;
                this.RasterRectangle(Left, Top, 188, 118, 16, Chosen ? "#b2c899" : "#1f3328", Chosen ? "#dcebc8" : null);
                this.RasterDesignSchematic(Design.Identifier, Left + 16, Top + 14, 156, 54, Chosen ? "#365035" : "#a9c49c");
                this.RasterText(Design.Name, Left + 16, Top + 94, 17, Chosen ? "#365035" : "#d4e3c8", "left", 500);
                this.RasterText(Design.Number, Left + 172, Top + 94, 13, Chosen ? "#4d6a48" : "#7f9a7a", "right");
                this.AdmitRegion("design:" + Design.Identifier, Left, Top, 188, 118, () => this.PresentDesign(Design.Identifier));
            }
        }
    }

    RasterDesignSchematic(Identifier, Left, Top, Width, Height, Colour)
    {
        // 📝 Miniature of each grid so the Settings picker shows the arrangement, not just a name.
        const Cell = (Across, Down, CellWidth, CellHeight) => this.RasterRectangle(Left + Across, Top + Down, CellWidth, CellHeight, 3, Colour);
        if (Identifier === "launchpad") { for (let Row = 0; Row < 2; ++Row) for (let Column = 0; Column < 6; ++Column) Cell(Column * 26, Row * 28, 20, 22); }
        else if (Identifier === "dashboard") { Cell(0, 0, 50, 54); for (let Row = 0; Row < 2; ++Row) for (let Column = 0; Column < 3; ++Column) Cell(58 + Column * 34, Row * 28, 30, 24); }
        else if (Identifier === "collections") { for (let Column = 0; Column < 3; ++Column) { Cell(Column * 54, 0, 48, 10); for (let Row = 0; Row < 2; ++Row) Cell(Column * 54, 16 + Row * 20, 48, 16); } }
        else if (Identifier === "focus") { Cell(50, 0, 56, 54); Cell(10, 14, 30, 30); Cell(116, 14, 30, 30); }
        else { Cell(0, 0, 40, 54); for (let Row = 0; Row < 4; ++Row) Cell(48, Row * 14, 108, 10); }
    }

    IncrementValue(Identifier, Difference, Minimum, Maximum)
    {
        this.Values[Identifier] = Math.max(Minimum, Math.min(Maximum, Math.round((this.Values[Identifier] + Difference) * 100) / 100));
        this.Dirty = true;
    }

    ReceiveText(Target)
    {
        this.TextTarget = Target;
        if (!this.ActiveTextInput) return;
        this.ActiveTextInput.value = Target === "message" ? this.MessageDraft : this.Nickname;
        this.ActiveTextInput.focus({ preventScroll: true });
        this.ActiveTextInput.setSelectionRange(this.ActiveTextInput.value.length, this.ActiveTextInput.value.length);
        this.Notice = Target === "message" ? "Type your message, then press Send or Enter." : "Type a name, then press Done or Enter.";
        this.Dirty = true;
    }

    AdmitText(Value)
    {
        if (this.TextTarget === "message") this.MessageDraft = Value.slice(0, 100);
        else if (this.TextTarget === "nickname") this.Nickname = Value.slice(0, 22);
        this.Dirty = true;
    }

    DeliverMessage()
    {
        if (!this.MessageDraft.trim()) return;
        this.Messages.push({ From: "You", Text: this.MessageDraft.trim() });
        this.MessageDraft = "";
        this.Notice = "Message added locally. Nothing was sent online.";
        if (this.ActiveTextInput) this.ActiveTextInput.value = "";
        this.Dirty = true;
    }

    RasterScreen()
    {
        if (!this.Dirty) return false;
        const Context = this.Context;
        Context.setTransform(this.Scale, 0, 0, this.Scale, this.OffsetAcross * this.Scale, this.OffsetDown * this.Scale);
        this.Regions = [];
        if (this.Application) this.RasterApplication();
        else if (this.Design === "dashboard") this.RasterDashboard();
        else if (this.Design === "collections") this.RasterCollections();
        else if (this.Design === "focus") this.RasterFocus();
        else if (this.Design === "console") this.RasterConsole();
        else this.RasterLaunchpad();
        if (this.DragMaterial && this.Pointer) {
            const M = this.DragMaterial, X = this.Pointer.PreviousAcross, Y = this.Pointer.PreviousDown;
            this.RasterRectangle(X-66, Y-25, 132, 50, 12, M.Colour || "#31516a", "#dcf5ff");
            this.RasterText(M.Kind === "paint" ? "Drop paint" : M.Kind === "finish" ? "Drop finish" : "Drop rim finish", X, Y+5, 14, "#ffffff", "center");
        }
        if (!this.ExternalBrightness && this.Values.brightness < 100)
        {
            Context.fillStyle = `rgba(0, 0, 0, ${(100 - this.Values.brightness) / 100})`;
            Context.fillRect(-this.OffsetAcross, -this.OffsetDown, this.PanelWidth, this.PanelHeight);
        }
        ++this.Revision;
        this.Dirty = false;
        return true;
    }

    QueryContact(Across, Down)
    {
        return [...this.Regions].reverse().find(Region => Across >= Region.Left && Across <= Region.Left + Region.Width &&
            Down >= Region.Top && Down <= Region.Top + Region.Height) || null;
    }

    ForwardPointer(Phase, Across, Down)
    {
        const Contact = this.QueryContact(Across, Down);
        if (Phase === "hover")
        {
            const Identifier = Contact?.Identifier || "";
            if (Identifier !== this.Hovered) { this.Hovered = Identifier; this.Dirty = true; }
            return Boolean(Contact);
        }
        if (Phase === "down")
        {
            this.Pointer = { Contact, Across, Down, LastAcross: Across, PreviousAcross: Across, PreviousDown: Down, Moved: false };
            this.Pressed = Contact?.Identifier || "";
            if (Contact?.Slider) this.AdvanceSlider(Contact, Across);
            this.Dirty = true;
            return true;
        }
        if (Phase === "move" && this.Pointer)
        {
            const Active = this.Pointer;
            Active.LastAcross = Across;
            if (Math.hypot(Across - Active.Across, Down - Active.Down) > 9) Active.Moved = true;
            if (Active.Contact?.Material && Active.Moved) {
                if (!this.DragMaterial && this.Preview) { Active.PriorSpin = this.Preview.Spin; this.Preview.Spin = false; }
                this.DragMaterial = Active.Contact.Material;
                this.Notice = "Drag onto the matching car surface; release to apply. Outside the car cancels.";
                this.Dirty = true;
            }
            if (Active.Contact?.Slider) this.AdvanceSlider(Active.Contact, Across);
            if (Active.Contact?.Orbit && this.Preview) { this.Preview.Orbit((Across - Active.PreviousAcross) * 0.012, (Down - Active.PreviousDown) * 0.008); this.Dirty = true; }
            Active.PreviousAcross = Across; Active.PreviousDown = Down;
            return true;
        }
        if (Phase === "up" && this.Pointer)
        {
            const Active = this.Pointer;
            if (Active.Contact?.Material && Active.Moved) {
                const Inside = Across >= 78 && Across <= 718 && Down >= 248 && Down <= 648;
                const Pick = Inside ? this.Preview?.Pick?.((Across-78)/640, (Down-248)/400) : null;
                const Material = Active.Contact.Material;
                const Valid = Material.Kind !== "wheel" ? Pick?.Family === CourseMaterial.BodyCoat
                    : Pick?.Occupant > 0 && [CourseMaterial.Hub, CourseMaterial.RimBase].includes(Pick?.Family);
                if (Valid) {
                    this.Appearance?.Apply(Material.Kind === "paint" ? { Colour: Material.Colour } : Material.Kind === "finish" ? { Matte: Material.Matte } : { DarkWheels: Material.DarkWheels, BlueRims: Material.BlueRims });
                    if (Material.Colour) this.FinishColour = Material.Colour;
                    this.Notice = "Material applied to the preview and driven car.";
                } else this.Notice = Inside ? "Drop paint on the body or rim material on a rim." : "Material drop cancelled.";
                this.DragMaterial = null;
            }
            else if (Active.Contact?.Swipe && Active.Moved && Math.abs(Across - Active.Across) > 45)
            {
                this.FocusIndex = (this.FocusIndex + (Across < Active.Across ? 1 : -1) + Applications.length) % Applications.length;
            }
            else if (!Active.Moved && Active.Contact && Contact?.Identifier === Active.Contact.Identifier)
                Active.Contact.Action?.();
            if (Active.PriorSpin !== undefined && this.Preview) this.Preview.Spin = Active.PriorSpin;
            this.Pointer = null;
            this.Pressed = "";
            this.Dirty = true;
            return true;
        }
        if (Phase === "cancel") { if (this.Pointer?.PriorSpin !== undefined && this.Preview) this.Preview.Spin = this.Pointer.PriorSpin; this.DragMaterial = null; this.Pointer = null; this.Pressed = ""; this.Dirty = true; }
        return false;
    }

    AdvanceSlider(Contact, Across)
    {
        const Fraction = Math.max(0, Math.min(1, (Across - Contact.TrackLeft) / Contact.TrackWidth));
        this.Values[Contact.Slider] = Math.round((Contact.Minimum + Fraction * (Contact.Maximum - Contact.Minimum)) / Contact.Step) * Contact.Step;
        this.Dirty = true;
    }

    QueryReading()
    {
        return { Design: this.Design, Application: this.Application, Revision: this.Revision,
            ApplicationCount: Applications.length, Values: { ...this.Values }, RoomReady: this.RoomReady,
            Nickname: this.Nickname, MessageCount: this.Messages.length, FocusIndex: this.FocusIndex,
            TextTarget: this.TextTarget, RegionCount: this.Regions.length };
    }
}

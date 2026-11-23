//============================================================================================================================================
//                                                              EDITORSHELL.JS
//============================================================================================================================================
// 🧩 The exhibit inside the Frontier Editor: outliner, viewport bar, inspector and status bar around the WebGPU viewport.

// 📝 The layout of the native editor (Outliner · Viewport · Inspector columns with tab headers, a status bar along the
//    bottom) drawn with the browser editor's styling (Style/FrontierEditor.css). The outliner lists the scene's
//    entities in groups — render, environment, vehicle, effects, destruction, level — with live state dots and the
//    eye toggles where an entity can be switched off; selecting one rebuilds the inspector from InspectorPages.
//    P hides both columns (the viewport takes the window), V hides everything, as before.

import { ConstructEntities, InspectionModes } from "./InspectorPages.js";
import { EditorControls, ClearElement, AssignStyle } from "./EditorControls.js";
import { ConstructGlyph } from "./EditorIcons.js";

const StorageKey = "IlluminationExhibit.Editor";
const GroupOrder = ["Render", "Environment", "Vehicle", "Effects", "Destruction", "Level"];
const LiveInterval = 200.0;   // [ms] readouts refresh at 5 Hz

function Construct(Tag, ClassName = "", Text = "")
{
    const Element = document.createElement(Tag);
    if (ClassName)
        Element.className = ClassName;
    if (Text)
        Element.textContent = Text;
    return Element;
}

export class EditorShell
{
    constructor(Host)
    {
        this.Host      = Host;
        this.Entities  = ConstructEntities(Host);
        this.Controls  = new EditorControls((Key, Target) => this.HandleChange(Key, Target));
        this.Query     = "";
        this.Collapsed = {};
        this.Rows      = new Map();
        this.LastLive  = 0.0;
        let Stored = null;
        try
        {
            Stored = JSON.parse(localStorage.getItem(StorageKey) || "null");
        }
        catch (Failure)
        {
            Stored = null;
        }
        this.Selected = Stored && this.Entities.some((Entity) => Entity.Id === Stored.Selected) ? Stored.Selected : "gas";
        this.ConstructOutliner();
        this.ConstructViewportBar();
        this.ConstructStatus();
        this.Select(this.Selected);
    }

    HandleChange(Key, Target)
    {
        this.Host.ApplySetting(Key, Target);
        this.RefreshRows();
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  OUTLINER
    //--------------------------------------------------------------------------------------------------------------------

    ConstructOutliner()
    {
        const Body = document.getElementById("outliner-body");
        ClearElement(Body);
        const Brand = Construct("div", "brand");
        const Symbol = Construct("span", "brand-symbol");
        Symbol.appendChild(ConstructGlyph("Layers", 24, 1.5));
        const Word = Construct("span", "", "frontier");
        Word.appendChild(Construct("span", "brand-dot", "."));
        Brand.append(Symbol, Word, Construct("span", "version", "ENGINE / 01"));
        Body.appendChild(Brand);
        const Label = Construct("div", "scene-label", "WORKSPACE");
        Label.appendChild(Construct("span", "status-dot"));
        Body.appendChild(Label);
        const Title = Construct("div", "scene-title", "Proving Grounds");
        Title.appendChild(Construct("span", "scene-extension", ".scene"));
        Body.appendChild(Title);

        const Tiles = Construct("div", "count-tiles");
        const Tile = (ClassName, Glyph, Caption) =>
        {
            const Element = Construct("div", `count-tile ${ClassName}`);
            const Icon = Construct("span", "count-icon");
            Icon.appendChild(ConstructGlyph(Glyph, 12, 2.4));
            const Value = Construct("span", "count-value", "0");
            Element.append(Icon, Construct("span", "count-caption", Caption), Value);
            Tiles.appendChild(Element);
            return Value;
        };
        this.VisibleCount = Tile("", "Check", "Enabled");
        this.HiddenCount = Tile("hidden-count", "EyeOff", "Disabled");
        Body.appendChild(Tiles);

        const Heading = Construct("div", "outliner-heading");
        const H2 = Construct("h2", "", "Outliner");
        this.EntityCount = Construct("span", "", String(this.Entities.length).padStart(2, "0"));
        H2.appendChild(this.EntityCount);
        Heading.appendChild(H2);
        Body.appendChild(Heading);

        const Search = Construct("label", "search");
        Search.appendChild(ConstructGlyph("Search", 14, 1.7));
        const Input = document.createElement("input");
        Input.placeholder = "Find an object…";
        Input.addEventListener("input", () => { this.Query = String(Input.value || "").toLowerCase(); this.RenderTree(); });
        Search.appendChild(Input);
        Body.appendChild(Search);

        this.Tree = Construct("div", "tree");
        Body.appendChild(this.Tree);

        const Bottom = Construct("div", "outliner-bottom");
        const World = Construct("div", "world-icon");
        World.appendChild(ConstructGlyph("Box", 18, 1.5));
        const Text = Construct("div");
        Text.append(Construct("strong", "", "IlluminationHost"), Construct("span", "", "Distance-field GI exhibit"));
        Bottom.append(World, Text, Construct("span", "status-dot"));
        Body.appendChild(Bottom);
        this.RenderTree();
    }

    RenderTree()
    {
        ClearElement(this.Tree);
        this.Rows.clear();
        const Matches = (Entity) => !this.Query || Entity.Name.toLowerCase().includes(this.Query) || Entity.Type.toLowerCase().includes(this.Query);
        for (const Group of GroupOrder)
        {
            const Members = this.Entities.filter((Entity) => Entity.Group === Group);
            const Visible = Members.filter((Entity) => Matches(Entity) || Members.some((Child) => Child.Parent === Entity.Id && Matches(Child)));
            if (Visible.length === 0)
                continue;
            const Section = Construct("div", "group");
            const Label = Construct("button", "group-label");
            const Open = !this.Collapsed[Group] || !!this.Query;
            Label.append(ConstructGlyph(Open ? "ChevronDown" : "ChevronRight", 12, 1.8), Construct("span", "", Group),
                         Construct("span", "group-count", String(Members.length)));
            Label.addEventListener("click", () => { this.Collapsed[Group] = !this.Collapsed[Group]; this.RenderTree(); });
            Section.appendChild(Label);
            if (Open)
                for (const Entity of Visible)
                    Section.appendChild(this.ConstructRow(Entity));
            this.Tree.appendChild(Section);
        }
        this.RefreshRows();
    }

    ConstructRow(Entity)
    {
        const Row = Construct("div", `tree-row${Entity.Parent ? " nested" : ""}${Entity.Id === this.Selected ? " selected" : ""}`);
        AssignStyle(Row, "--entity-colour", Entity.Colour);
        const Button = Construct("button", "object-button");
        Button.appendChild(ConstructGlyph(Entity.Glyph, 16, 1.6));
        const Meta = Construct("span", "row-meta");
        Button.append(Construct("span", "row-name", Entity.Name), Meta);
        Button.addEventListener("click", () => this.Select(Entity.Id));
        const State = Construct("span", "row-state");
        Row.append(Button, State);
        let Eye = null;
        if (Entity.Toggle)
        {
            Eye = Construct("button", "visibility");
            Eye.setAttribute("aria-label", `Toggle ${Entity.Name}`);
            Eye.addEventListener("click", () =>
            {
                Entity.Toggle.Write(!Entity.Toggle.Read());
                this.Host.ApplySetting(`Toggle:${Entity.Id}`, null);
                this.Controls.Synchronise();
                this.RefreshRows();
            });
            Row.appendChild(Eye);
        }
        this.Rows.set(Entity.Id, { Row, Meta, State, Eye, Entity, EyeOn: null });
        return Row;
    }

    RefreshRows()
    {
        let Enabled = 0, Disabled = 0;
        for (const Entity of this.Entities)
        {
            const On = Entity.Toggle ? !!Entity.Toggle.Read() : true;
            if (On) Enabled++; else Disabled++;
        }
        this.VisibleCount.textContent = String(Enabled);
        this.HiddenCount.textContent = String(Disabled);
        for (const Record of this.Rows.values())
        {
            const On = Record.Entity.Toggle ? !!Record.Entity.Toggle.Read() : true;
            Record.Meta.textContent = Record.Entity.Meta ? Record.Entity.Meta() : "";
            Record.State.className = `row-state ${On ? (Record.Entity.State ? Record.Entity.State() : "") : ""}`;
            Record.Row.classList.toggle("is-off", !On);
            Record.Row.classList.toggle("selected", Record.Entity.Id === this.Selected);
            if (Record.Eye && Record.EyeOn !== On)
            {
                ClearElement(Record.Eye);
                Record.Eye.appendChild(ConstructGlyph(On ? "Eye" : "EyeOff", 13, 1.7));
                Record.EyeOn = On;
            }
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                 INSPECTOR
    //--------------------------------------------------------------------------------------------------------------------

    Select(Id)
    {
        const Entity = this.Entities.find((Candidate) => Candidate.Id === Id) || this.Entities[0];
        this.Selected = Entity.Id;
        try
        {
            localStorage.setItem(StorageKey, JSON.stringify({ Selected: this.Selected }));
        }
        catch (Failure)
        {
        }
        this.ConstructInspector(Entity);
        this.RefreshRows();
    }

    ConstructInspector(Entity)
    {
        const Top = document.getElementById("inspector-top");
        ClearElement(Top);
        const Crumbs = Construct("div", "crumbs", "Inspector");
        Crumbs.append(ConstructGlyph("ChevronRight", 12, 1.8), Construct("span", "", Entity.Group));
        const Status = Construct("div", "save-status");
        Status.append(ConstructGlyph("Check", 12, 2), Construct("span", "", "Changes apply live"));
        Top.append(Crumbs, Status);

        const Content = document.getElementById("inspector-content");
        ClearElement(Content);
        this.Controls.Reset();
        const Header = Construct("div", "object-header");
        const Titles = Construct("div");
        Titles.append(Construct("div", "eyebrow", Entity.Type), Construct("h1", "", Entity.Name));
        const Actions = Construct("div", "object-actions");
        if (Entity.Toggle)
        {
            const Pill = Construct("button", "enabled-pill");
            const Dot = Construct("span");
            const Caption = Construct("em", "", "");
            Caption.style.fontStyle = "normal";
            Pill.append(Dot, Caption);
            const Show = () =>
            {
                const On = !!Entity.Toggle.Read();
                Pill.classList.toggle("disabled", !On);
                Caption.textContent = On ? "Enabled" : "Disabled";
            };
            Pill.addEventListener("click", () =>
            {
                Entity.Toggle.Write(!Entity.Toggle.Read());
                this.Host.ApplySetting(`Toggle:${Entity.Id}`, null);
                this.Controls.Synchronise();
                this.RefreshRows();
            });
            Show();
            this.Controls.Bindings.push(Show);
            Actions.appendChild(Pill);
        }
        Header.append(Titles, Actions);
        Content.appendChild(Header);
        const Section = Construct("div", "section-label");
        Section.append(Construct("span", "", "PROPERTIES"), Construct("span", "", Entity.Description || ""));
        Content.appendChild(Section);
        if (Entity.Switches)
            this.Controls.PropertySwitches(Content, Entity.Switches());
        const Cards = Construct("div", "cards");
        Content.appendChild(Cards);
        Entity.Build(this.Controls, Cards);
        const Footer = Construct("footer", "inspector-footer");
        const Left = Construct("span");
        Left.append(Construct("span", "footer-dot"), Construct("span", "", "Changes apply in real time"));
        Footer.append(Left, Construct("span", "", `${Entity.Name} / ${Entity.Type}`));
        Content.appendChild(Footer);
        if (Content.scrollTo)
            Content.scrollTo(0, 0);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                               VIEWPORT, STATUS
    //--------------------------------------------------------------------------------------------------------------------

    ConstructViewportBar()
    {
        const Bar = document.getElementById("viewport-bar");
        ClearElement(Bar);
        const Heading = Construct("div", "scene-heading");
        Heading.append(Construct("strong", "", "Scene"), Construct("span", "", "WebGPU · distance-field GI · ReSTIR"));
        Bar.append(Heading, Construct("span", "live-pill", "LIVE"));
        const Host = this.Host;
        const Camera = Construct("div", "segmented");
        Camera.style.width = "190px";
        this.CameraButtons = [["Chase", false], ["Free · F8", true]].map(([Caption, Free]) =>
        {
            const Button = Construct("button", "", Caption);
            Button.addEventListener("click", () => Host.AssignCamera(Free));
            Camera.appendChild(Button);
            return [Button, Free];
        });
        Bar.appendChild(Camera);
        const Display = document.createElement("select");
        Display.className = "select-field";
        Display.style.width = "210px";
        for (const [Caption, Value] of InspectionModes)
        {
            const Choice = document.createElement("option");
            Choice.value = String(Value);
            Choice.textContent = Caption;
            Display.appendChild(Choice);
        }
        Display.addEventListener("change", () => { Host.Settings.Inspection = Number(Display.value); Host.ApplySetting("Inspection"); this.Controls.Synchronise(); });
        this.DisplaySelect = Display;
        Bar.append(Display, Construct("span", "bar-fill"));
        const Icon = (Glyph, Title, Action) =>
        {
            const Button = Construct("button", "icon-button");
            Button.title = Title;
            Button.appendChild(ConstructGlyph(Glyph, 15, 1.7));
            Button.addEventListener("click", Action);
            Bar.appendChild(Button);
            return Button;
        };
        this.HudButton = Icon("Activity", "Readout (H)", () => Host.Dock.Toggle("Hud"));
        this.InsetButton = Icon("GridLattice", "Distance slice (I)", () => Host.Dock.Toggle("Inset"));
        Icon("RotateCcw", "Respawn (R)", () => Host.RespawnVehicle());
        Icon("Undo2", "Reassemble every prop", () => Host.Fracture.ReassembleAll());
        Icon("Scan", "Clean view (V)", () => Host.Dock.ToggleClean());
    }

    ConstructStatus()
    {
        const Status = document.getElementById("status");
        ClearElement(Status);
        this.StatusItems = {};
        const Item = (Key, Label) =>
        {
            const Element = Construct("div", "status-item");
            const Value = Construct("strong", "", "–");
            Element.append(Construct("span", "", Label), Value);
            Status.appendChild(Element);
            this.StatusItems[Key] = Value;
        };
        Item("Rate", "REALTIME");
        Item("Frame", "FRAME");
        Item("Quality", "QUALITY");
        Item("Sun", "SUN");
        Item("Speed", "SPEED");
        Item("Smoke", "SMOKE");
        Item("Fragments", "FRAGMENTS");
        Item("Camera", "CAM");
        Status.appendChild(Construct("span", "status-fill"));
        Item("Adapter", "GPU");
    }

    // 📝 Called every frame by the host; the readouts, the outliner and the status bar refresh at LiveInterval.
    Present(Now, HudLines)
    {
        if (Now - this.LastLive < LiveInterval)
            return;
        this.LastLive = Now;
        const Host = this.Host;
        document.getElementById("hud-text").textContent = HudLines.join("\n");
        const Telemetry = Host.Vehicle.QueryTelemetry();
        const Items = this.StatusItems;
        Items.Rate.textContent = `${(1.0 / Math.max(Host.FrameSeconds || 1, 1.0e-4)).toFixed(0)} fps`;
        Items.Frame.textContent = `${((Host.FrameSeconds || 0) * 1000.0).toFixed(1)} ms`;
        Items.Quality.textContent = Host.GradeName;
        Items.Sun.textContent = `${Host.Settings.SunElevation.toFixed(1)}°`;
        Items.Speed.textContent = `${Math.abs(Telemetry.Speed * 3.6).toFixed(0)} km/h${Telemetry.Burnout ? " · burnout" : ""}`;
        Items.Smoke.textContent = Host.Smoke.Resident ? `resident · ${Host.Smoke.Metrics.Sources} src` : "idle";
        Items.Fragments.textContent = String(Host.Fracture.Metrics.ActiveFragments);
        const Eye = Host.CameraEye || [0, 0, 0];
        Items.Camera.textContent = Eye.map((Component) => Component.toFixed(0)).join(", ");
        Items.Adapter.textContent = Host.AdapterDescription || "";
        for (const [Button, Free] of this.CameraButtons)
            Button.classList.toggle("active", Host.FreeCamera === Free);
        this.DisplaySelect.value = String(Host.Settings.Inspection);
        this.HudButton.classList.toggle("active", Host.Dock.QueryVisible("Hud"));
        this.InsetButton.classList.toggle("active", Host.Dock.QueryVisible("Inset"));
        this.Controls.RefreshLive();
        this.RefreshRows();
    }

    // 📝 A short notice at the top of the viewport (a prop shattered, a teleport…).
    Announce(Text)
    {
        const Notice = document.getElementById("event-notice");
        if (!Notice)
            return;
        Notice.textContent = Text;
        Notice.classList.remove("hidden");
        clearTimeout(this.NoticeTimer);
        this.NoticeTimer = setTimeout(() => Notice.classList.add("hidden"), 2400);
    }
}

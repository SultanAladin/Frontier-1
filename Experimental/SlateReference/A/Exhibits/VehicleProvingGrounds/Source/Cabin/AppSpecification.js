//============================================================================================================================================
//                                                          APPSPECIFICATION.JS
//============================================================================================================================================
// 📦 Launcher applications, design identities and fixed device-screen coordinates.

export const ScreenWidth = 1200;
export const ScreenHeight = 750;

export const Applications = [
    { Identifier: "vehicle", Name: "Vehicle", Caption: "Make it your own", Category: "Vehicle", Colour: "#a4e5be" },
    { Identifier: "drive-room", Name: "Drive Room", Caption: "Your people. Your race.", Category: "Connect", Colour: "#ffcf87", Badge: "3" },
    { Identifier: "messages", Name: "Messages", Caption: "Keep the conversation going", Category: "Connect", Colour: "#94caff", Badge: "2" },
    { Identifier: "contacts", Name: "Contacts", Caption: "People worth driving with", Category: "Connect", Colour: "#f4b19b" },
    { Identifier: "navigation", Name: "Navigation", Caption: "Find your next road", Category: "Vehicle", Colour: "#8fe0d0" },
    { Identifier: "media", Name: "Media", Caption: "Set the pace", Category: "Personal", Colour: "#efa5d7" },
    { Identifier: "tyres", Name: "Tyres", Caption: "Every corner counts", Category: "Vehicle", Colour: "#aad9e9" },
    { Identifier: "telemetry", Name: "Telemetry", Caption: "Know your machine", Category: "Vehicle", Colour: "#cbb2ef" },
    { Identifier: "climate", Name: "Climate", Caption: "Your comfort zone", Category: "Personal", Colour: "#a2dbe7" },
    { Identifier: "garage", Name: "Garage", Caption: "A place for your car", Category: "Personal", Colour: "#e5bf95" },
    { Identifier: "account", Name: "Account", Caption: "Always your space", Category: "Connect", Colour: "#b9b4f6" },
    { Identifier: "settings", Name: "Settings", Caption: "The little things", Category: "Personal", Colour: "#c5cfd2" }
];

export const LauncherDesigns = [
    { Identifier: "launchpad", Name: "Launchpad", Caption: "The familiar icon canvas", Number: "01" },
    { Identifier: "dashboard", Name: "Dashboard", Caption: "Big moments. Small cards.", Number: "02" },
    { Identifier: "collections", Name: "Collections", Caption: "A place for everything", Number: "03" },
    { Identifier: "focus", Name: "Focus", Caption: "One app takes the stage", Number: "04" },
    { Identifier: "console", Name: "Console", Caption: "A calm rail. A clear choice.", Number: "05" }
];

export function ResolveApplication(Identifier)
{
    return Applications.find(Application => Application.Identifier === Identifier);
}

export function ResolveDesign(Identifier)
{
    return LauncherDesigns.find(Design => Design.Identifier === Identifier);
}

export function ConstructDesignGlyph(Identifier)
{
    const Start = '<svg class="DesignGlyph" viewBox="0 0 48 34" aria-hidden="true"><rect x="1" y="1" width="46" height="32" rx="5" fill="#dee7d8"/>';
    const Square = (Left, Top, Width = 7, Height = 6, Colour = "#8da48a") => `<rect x="${Left}" y="${Top}" width="${Width}" height="${Height}" rx="1.8" fill="${Colour}"/>`;
    let Content = "";
    if (Identifier === "launchpad")
    {
        for (let Row = 0; Row < 2; ++Row)
            for (let Column = 0; Column < 4; ++Column) Content += Square(6 + Column * 9, 8 + Row * 10);
    }
    else if (Identifier === "dashboard")
    {
        Content += Square(5, 6, 17, 22, "#68896e");
        Content += Square(25, 6, 17, 9);
        Content += Square(25, 18, 7, 10);
        Content += Square(35, 18, 7, 10);
    }
    else if (Identifier === "collections")
    {
        for (let Column = 0; Column < 3; ++Column)
        {
            Content += Square(5 + Column * 13, 6, 11, 22, "#b8c8ad");
            Content += Square(7 + Column * 13, 10, 7, 3, "#759076");
            Content += Square(7 + Column * 13, 17, 7, 3, "#759076");
            Content += Square(7 + Column * 13, 24, 7, 2, "#759076");
        }
    }
    else if (Identifier === "focus")
    {
        Content += Square(2, 10, 7, 15, "#bdcbb5");
        Content += Square(12, 6, 23, 22, "#759076");
        Content += Square(38, 10, 8, 15, "#bdcbb5");
    }
    else
    {
        Content += Square(4, 5, 9, 24, "#7e987b");
        for (let Row = 0; Row < 3; ++Row)
            for (let Column = 0; Column < 2; ++Column) Content += Square(17 + Column * 13, 7 + Row * 7, 10, 5);
    }
    return Start + Content + "</svg>";
}

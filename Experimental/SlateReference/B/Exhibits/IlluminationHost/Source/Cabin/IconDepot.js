//============================================================================================================================================
//                                                              ICONDEPOT.JS
//============================================================================================================================================
// 📦 Original multicolour SVG application artwork and its reconstructible browser bitmaps.

const Artwork = {
    vehicle: `<path d="m13 27 4-11a5 5 0 0 1 5-3h20a5 5 0 0 1 5 3l4 11" fill="#c6f5cc"/><path d="m19 24 3-8h20l3 8" fill="#385f75"/><rect x="10" y="24" width="44" height="24" rx="8" fill="#84d99f"/><rect x="14" y="46" width="7" height="7" rx="2" fill="#31504b"/><rect x="43" y="46" width="7" height="7" rx="2" fill="#31504b"/><path d="m14 29 9 2m18 0 9-2" stroke="#f9ffcf" stroke-width="4" stroke-linecap="round"/><path d="M26 40h12" stroke="#355b59" stroke-width="3" stroke-linecap="round"/>`,
    "drive-room": `<path d="M14 17v36" stroke="#71546c" stroke-width="4" stroke-linecap="round"/><path d="M17 15h31v23H17z" fill="#fff0cb"/><path d="M17 15h8v8h-8zm16 0h8v8h-8zm-8 8h8v8h-8zm16 0h7v8h-7zm-24 8h8v7h-8zm16 0h8v7h-8z" fill="#a87391"/><path d="M26 47h24" stroke="#ea9468" stroke-width="4" stroke-linecap="round"/><path d="m43 41 7 6-7 6" stroke="#fff0cf" stroke-width="3" fill="none" stroke-linecap="round" stroke-linejoin="round"/>`,
    messages: `<path d="M13 15h33a8 8 0 0 1 8 8v17a8 8 0 0 1-8 8H28L15 56l3-8a8 8 0 0 1-5-8z" fill="#397ebe"/><path d="M10 11h31a8 8 0 0 1 8 8v16a8 8 0 0 1-8 8H25l-12 8 2-9a8 8 0 0 1-5-7z" fill="#eff8fd"/><circle cx="20" cy="27" r="3" fill="#689fc6"/><circle cx="30" cy="27" r="3" fill="#68b6ac"/><circle cx="40" cy="27" r="3" fill="#e6b562"/>`,
    contacts: `<circle cx="40" cy="24" r="9" fill="#b388a0"/><path d="M28 49v-6a12 12 0 0 1 24 0v6" fill="#8e7399"/><circle cx="25" cy="22" r="11" fill="#fff0d2"/><path d="M9 51v-8a16 16 0 0 1 32 0v8" fill="#fff0d2"/><path d="M15 45c7-5 14-5 20 0" stroke="#ecaa87" stroke-width="3" stroke-linecap="round"/>`,
    navigation: `<path d="m9 15 15-5 16 6 15-5v40l-15 5-16-6-15 5z" fill="#edf6cf"/><path d="M24 10v40l16 6V16z" fill="#bbdfbd"/><path d="m12 37 14-8 12 8 13-16" stroke="#519d9c" stroke-width="4" fill="none" stroke-linecap="round" stroke-linejoin="round"/><path d="M46 15c-9 0-11 10-1 19 10-9 10-19 1-19z" fill="#ee7d75"/><circle cx="46" cy="22" r="3" fill="#fff3d9"/>`,
    media: `<path d="M23 17v29m0-29 24-5v29" stroke="#fff0e3" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/><ellipse cx="17" cy="46" rx="9" ry="7" fill="#fff0e3"/><ellipse cx="41" cy="42" rx="9" ry="7" fill="#7b5397"/><path d="m26 24 18-4" stroke="#9959a3" stroke-width="4" stroke-linecap="round"/>`,
    tyres: `<ellipse cx="30" cy="33" rx="19" ry="24" fill="#3d6070"/><ellipse cx="30" cy="33" rx="11" ry="16" fill="#d7f3ec"/><ellipse cx="30" cy="33" rx="5" ry="8" fill="#6c9fb5"/><path d="m14 21 7 3m-9 9 7 1m-6 10 7-2m18-21 7-3m-6 17 7-1m-8 10 7 2" stroke="#78a1b2" stroke-width="2.8" stroke-linecap="round"/><circle cx="49" cy="48" r="9" fill="#dbb164"/><path d="M49 43v6m0 4h.01" stroke="#fff2d3" stroke-width="2.5" stroke-linecap="round"/>`,
    telemetry: `<path d="M12 12v39h41" stroke="#f0e9f9" stroke-width="4" fill="none" stroke-linecap="round"/><path d="m19 41 9-14 10 8 13-18" stroke="#6f589b" stroke-width="4" fill="none" stroke-linecap="round" stroke-linejoin="round"/><circle cx="28" cy="27" r="4" fill="#a4dfd0"/><circle cx="38" cy="35" r="4" fill="#efd395"/><circle cx="51" cy="17" r="4" fill="#f0e9f9"/>`,
    climate: `<path d="M32 27c-5-14-1-22 7-18 9 4 5 15-7 18zm5 7c15-3 22 1 17 9-5 8-15 3-17-9zm-8 3c-2 15-9 20-15 13-7-7 0-16 15-13zm-4-9C11 22 5 25 8 34c3 9 14 6 17-6z" fill="#ecf8f3"/><circle cx="32" cy="32" r="8" fill="#659ba7"/><circle cx="32" cy="32" r="4" fill="#e9c482"/>`,
    garage: `<path d="m8 27 24-17 24 17v28H8z" fill="#f7e5c5"/><path d="M16 28h32v27H16z" fill="#ac8c78"/><path d="M18 34h28m-28 6h28" stroke="#d5b597" stroke-width="2"/><path d="m20 48 3-6h18l3 6v7H20z" fill="#8fbcc9"/><path d="m25 45 1-3h12l1 3" fill="#385f76"/><circle cx="24" cy="51" r="2" fill="#fff0cc"/><circle cx="40" cy="51" r="2" fill="#fff0cc"/>`,
    account: `<rect x="9" y="10" width="46" height="45" rx="11" fill="#7774b2"/><circle cx="31" cy="25" r="10" fill="#f5d9bd"/><path d="M15 48a16 16 0 0 1 32 0" fill="#eff0e2"/><circle cx="49" cy="49" r="9" fill="#a0d5b7"/><path d="m45 49 3 3 5-6" stroke="#41796b" stroke-width="2.5" fill="none" stroke-linecap="round" stroke-linejoin="round"/>`,
    settings: `<path d="m28 9 8 0 2 7 6 3 7-2 4 7-5 5v6l5 5-4 7-7-2-6 3-2 7h-8l-2-7-6-3-7 2-4-7 5-5v-6l-5-5 4-7 7 2 6-3z" fill="#f0eee0"/><circle cx="32" cy="32" r="12" fill="#668486"/><circle cx="32" cy="32" r="6" fill="#b1ddd2"/><circle cx="48" cy="17" r="5" fill="#eaa681"/>`
};

const Backgrounds = {
    vehicle: ["#b5ebc6", "#67bd89"], "drive-room": ["#f9d894", "#e9b16c"], messages: ["#9bd5f3", "#6ca9df"],
    contacts: ["#f5c7ab", "#e3917e"], navigation: ["#b3e6cf", "#70c3bd"], media: ["#f0b9dd", "#d587bd"],
    tyres: ["#b7dee3", "#89bfce"], telemetry: ["#d8c8f0", "#a48ccb"], climate: ["#b1e3e9", "#7abbc8"],
    garage: ["#edd7b5", "#c99c74"], account: ["#ceccf1", "#9b96cc"], settings: ["#dbe1dd", "#a4b6b7"]
};

export function ResolveIconSvg(Identifier)
{
    const Colours = Backgrounds[Identifier];
    if (!Colours || !Artwork[Identifier]) throw new Error("Unknown application icon: " + Identifier);
    return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><defs><linearGradient id="Surface" x1="0" y1="0" x2="1" y2="1"><stop stop-color="${Colours[0]}"/><stop offset="1" stop-color="${Colours[1]}"/></linearGradient></defs><rect x="1" y="1" width="62" height="62" rx="15" fill="url(#Surface)"/>${Artwork[Identifier]}</svg>`;
}

export async function ConstructIconImages(Applications)
{
    const Images = {};
    await Promise.all(Applications.map(Application => new Promise((Resolve, Reject) =>
    {
        const Picture = new Image();
        Picture.onload = () => { Images[Application.Identifier] = Picture; Resolve(); };
        Picture.onerror = () => Reject(new Error("SVG icon could not be decoded: " + Application.Identifier));
        Picture.src = "data:image/svg+xml;charset=utf-8," + encodeURIComponent(ResolveIconSvg(Application.Identifier));
    })));
    return Images;
}

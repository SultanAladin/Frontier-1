"""Six native-SVG weather and environment controls with shaded depth."""


def snowflake(x,y,r,prefix):
    body=f'<g transform="translate({x} {y}) scale({r/40})" stroke-linecap="round" stroke-linejoin="round">'
    for angle in range(0,360,60):
        body+=f'<g transform="rotate({angle})"><path d="M0 0V-40M0-26l-10-9m10 9 10-9M0-13l-7-7m7 7 7-7" stroke="#{prefix}" stroke-width="4.5"/><path d="M-1-3v-34" stroke="#f0fcff" stroke-width="1.1" opacity=".7"/></g>'
    return body+'<path d="M0-7 6-3v7L0 8-6 4v-7Z" fill="#eefaff" stroke="none"/></g>'


def register_weather_expansion_icons(icon):
    from weather_particle_revision import rain_art, hail_art, atmosphere_art
    defs,body=rain_art()
    icon('weather-rain','Rain','Environment','A field of falling water particles: small rounded droplets, fine slanted trails and restrained liquid highlights. No cloud or pointed teardrop symbols.',defs,body)

    defs='''<radialGradient id="weather-snow-halo"><stop stop-color="#b9e7ff" stop-opacity=".14"/><stop offset="1" stop-color="#86ccf0" stop-opacity="0"/></radialGradient>'''
    body='<circle cx="114" cy="125" r="86" fill="url(#weather-snow-halo)"/>'
    body+=snowflake(111,126,65,'a1cce9')+snowflake(191,65,23,'b7ddec')+snowflake(194,183,22,'86b5d5')
    for x,y,r in [(53,58,3),(170,121,2.5),(70,208,3),(154,208,2),(208,119,2)]:body+=f'<circle cx="{x}" cy="{y}" r="{r}" fill="#d7edf5"/>'
    icon('weather-snow','Snow','Environment','A six-armed crystalline snowflake with branching ice facets, two smaller flakes and falling snow points.',defs,body)

    defs,body=hail_art()
    icon('weather-hail','Hail','Environment','Irregular rounded hailstones with milky frozen interiors, small fractures and faint falling trails. No cloud or uniform polished balls.',defs,body)

    defs,body=atmosphere_art()
    icon('sky-scattering','Atmosphere layers','Environment','A shaded sphere surrounded by a continuous pale-cyan, blue and violet atmospheric gradient fading into space. Schematic altitude transitions, no hard layer boundaries, cutaway or sun.',defs,body)

    defs='''<linearGradient id="exposure-rim" x2=".8" y2="1"><stop stop-color="#d4dce0"/><stop offset=".45" stop-color="#778594"/><stop offset="1" stop-color="#3b485b"/></linearGradient><linearGradient id="exposure-lit" x2=".8" y2="1"><stop stop-color="#fffbed"/><stop offset="1" stop-color="#c8c9bd"/></linearGradient><linearGradient id="exposure-dark" x2=".8" y2="1"><stop stop-color="#586372"/><stop offset="1" stop-color="#1a2638"/></linearGradient>'''
    body='''<circle cx="128" cy="130" r="74" fill="url(#exposure-rim)"/><circle cx="128" cy="130" r="66" fill="url(#exposure-dark)"/><path d="M128 64a66 66 0 0 1 0 132Z" fill="url(#exposure-lit)"/><circle cx="128" cy="130" r="66" stroke="#e4eef2" stroke-opacity=".25"/><path d="M96 114v32m-16-16h32" stroke="#e5e8e5" stroke-width="5" stroke-linecap="round"/><path d="M146 130h30" stroke="#4b5664" stroke-width="5" stroke-linecap="round"/><path d="M70 67a86 86 0 0 1 116 0" stroke="#b8c9d8" stroke-width="2" fill="none" stroke-linecap="round"/><path d="M128 32v8m-40 2 4 7m72-7-4 7" stroke="#dce6ee" stroke-width="3" stroke-linecap="round"/>'''
    icon('environment-exposure','Exposure','Environment','A dimensional exposure dial with a split light/dark face, plus/minus controls and a restrained metering arc. Native SVG.',defs,body)

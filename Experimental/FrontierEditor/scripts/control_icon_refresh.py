"""Simple wind, dense point stars, automotive LEDs and bug-style debugging."""
import random


def revisions():
    wind='''<g stroke="#b7d7dc" stroke-width="8" stroke-linecap="round" stroke-linejoin="round"><path d="M43 102h112c34 0 35-40 11-40-13 0-19 10-16 19"/><path d="M34 130h157c33 0 33 39 10 39-12 0-19-8-18-17"/><path d="M58 158h61c25 0 27 33 7 33"/></g>'''
    rng=random.Random(71)
    points=[]
    while len(points)<62:
        x,y=rng.uniform(35,221),rng.uniform(35,221)
        if ((x-128)/100)**2+((y-128)/100)**2>1:continue
        if any((x-a)**2+(y-b)**2<13**2 for a,b in points):continue
        points.append((x,y))
    stars=''.join(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{rng.uniform(1.25,3.0):.2f}" fill="{rng.choice(["#f3f5f8","#d5dfea","#b9c8da"])}"/>' for x,y in points)
    strip='''<path d="M40 107h145q18 0 27-16l6-10v51q0 19-23 19H40q-10 0-10-10v-24q0-10 10-10Z" fill="#303a45" stroke="#8294a4" stroke-width="2"/><path d="M45 120h140q21 0 25-13v22q0 9-16 9H45Z" fill="#f1faff"/><g stroke="#526576" stroke-width="3">'''+''.join(f'<path d="M{x} 120v18"/>' for x in range(65,186,24))+'''</g><path d="M61 96v-13m40 13V78m40 18V83" stroke="#bfdeed" stroke-width="4" stroke-linecap="round"/>'''
    return {
      'wind':('Three clean rounded airflow strokes. A simple solid-color wind control with no glow or gradients.','',wind),
      'stars':('A fuller sky of 62 clean star dots, with varied sizes and no glow, rays or flares.','',stars),
      'light-led-strip-2d':('A flat segmented automotive DRL with a swept outer tip, bright white emitters and restrained graphite housing.','',strip),
    }


def register_control_icons(icon):
    lamp='''<path d="M37 89q2-12 17-12h165q9 0 5 10l-21 68q-6 20-31 23H70q-28-1-33-27Z" fill="#333b43" stroke="#7b8790" stroke-width="3"/><path d="M49 91h162l-18 59q-4 13-23 15H71q-18-1-21-17Z" fill="#181f26"/><path d="M58 100h131l-13 38q-3 8-13 8H74q-13 0-16-12" stroke="#8e9ba4" stroke-width="2"/><path d="M51 93h151l-17 52q-4 12-20 13H71q-14 0-19-12" stroke="#f2fbff" stroke-width="7" stroke-linecap="round" stroke-linejoin="round"/><path d="M61 84h141" stroke="#b6c5cc" stroke-width="2" stroke-linecap="round"/>
<circle cx="92" cy="121" r="20" fill="#465663"/><circle cx="92" cy="121" r="15" fill="#c3d8e4"/><circle cx="92" cy="121" r="10" fill="#effaff"/><path d="M80 121a12 12 0 0 1 18-10" stroke="#fff" stroke-width="2" stroke-linecap="round"/>
<path d="M126 108h14v23h-14Zm22 0h14v23h-14Z" fill="#f0f9fd"/><path d="M126 137h35" stroke="#687d8a" stroke-width="2"/><path d="m207 97-9 27" stroke="#bbd4df" stroke-width="3" stroke-linecap="round"/>'''
    icon('light-car-led','Car LED headlamp','Lights','A standalone swept automotive LED headlamp: graphite housing, white perimeter DRL, projector lens and two LED cells. No car silhouette.','',lamp)
    for slug,name,variant in [('debug-bug','Debug bug',None),('debug-run','Run debugger','run'),('debug-breakpoint','Debug breakpoint','breakpoint')]:
        # A diagnostic beetle, not a cartoon insect: split plates and exposed circuitry.
        bug='''<g fill="none" stroke="#81949f" stroke-width="6" stroke-linejoin="round" stroke-linecap="round"><path d="M89 111H72L58 96m27 44H51m39 28H72l-14 19M167 111h17l14-15m-27 44h34m-39 28h18l14 19"/><path d="M114 75V60l-11-12m39 27V60l11-12" stroke-width="4"/></g>
<rect x="52" y="88" width="10" height="10" rx="3" fill="#93c7c9"/><rect x="45" y="135" width="10" height="10" rx="3" fill="#93c7c9"/><rect x="52" y="185" width="10" height="10" rx="3" fill="#93c7c9"/><rect x="194" y="88" width="10" height="10" rx="3" fill="#93c7c9"/><rect x="201" y="135" width="10" height="10" rx="3" fill="#93c7c9"/><rect x="194" y="185" width="10" height="10" rx="3" fill="#93c7c9"/>
<path d="M108 76q0-14 20-14t20 14v16h-40Z" fill="#465660"/><rect x="116" y="73" width="24" height="4" rx="2" fill="#d9bb7d"/>
<path d="M122 88c-28 0-40 17-40 46v26c0 26 13 42 40 49Z" fill="#465a65" stroke="#8399a5" stroke-width="1.8"/><path d="M134 88c28 0 40 17 40 46v26c0 26-13 42-40 49Z" fill="#33434e" stroke="#708692" stroke-width="1.8"/>
<path d="M112 101c-13 4-20 16-20 34v21q0 27 20 37Z" fill="#65969e"/><path d="M144 101c13 4 20 16 20 34v21q0 27-20 37Z" fill="#48747f"/>
<path d="M99 122v25l7 7v22m51-54v25l-7 7v22" fill="none" stroke="#bbe0dd" stroke-opacity=".65" stroke-width="2" stroke-linecap="round"/>
<rect x="124" y="100" width="8" height="16" rx="2" fill="#a5d8d2"/><rect x="124" y="123" width="8" height="8" rx="2" fill="#e2bf7d"/><rect x="124" y="138" width="8" height="42" rx="2" fill="#77939e"/><path d="M95 112q4-10 12-13" stroke="#b9d6da" stroke-width="2" stroke-linecap="round" fill="none"/>'''
        if variant:
            bug+='<rect x="161" y="167" width="48" height="44" rx="13" fill="#202c35" stroke="#9caeb6" stroke-width="2"/>'
            bug+=('<path d="m179 177 17 12-17 12Z" fill="#a2ddd0"/>' if variant=='run' else '<path d="m185 176 13 13-13 13-13-13Z" fill="#e18f89"/><rect x="183" y="182" width="4" height="14" rx="1" fill="#59363a"/>')
        icon('editor-'+slug,name,'Editor','A graphite circuit beetle with split shell plates, cyan trace inlays, six terminal-ended legs and an amber diagnostic indicator'+(' plus a '+variant+' control.' if variant else '.'),'',bug)

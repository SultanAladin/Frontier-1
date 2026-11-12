"""Particle-only precipitation and schematic atmospheric shells."""
import random


def rain_art():
    defs='''<linearGradient id="rain-fall" x1="1" y1="0" x2="0" y2="1"><stop stop-color="#a5d1e7" stop-opacity="0"/><stop offset=".7" stop-color="#b9e2f2" stop-opacity=".55"/><stop offset="1" stop-color="#eefaff" stop-opacity=".9"/></linearGradient><radialGradient id="rain-liquid" cx="30%" cy="24%" r="82%"><stop stop-color="#f0fbff" stop-opacity=".95"/><stop offset=".35" stop-color="#9fc6dc" stop-opacity=".6"/><stop offset=".7" stop-color="#557d9f" stop-opacity=".35"/><stop offset="1" stop-color="#c8e9f4" stop-opacity=".85"/></radialGradient>'''
    rng=random.Random(98);body=''
    for row in range(5):
        for col in range(5):
            x=49+col*37+rng.uniform(-9,9);y=52+row*36+rng.uniform(-8,8)
            r=rng.uniform(1.8,3.6);length=rng.uniform(11,27)
            body+=f'<g opacity="{rng.uniform(.5,1):.2f}"><path d="M{x+length*.24:.2f} {y-length:.2f} {x:.2f} {y:.2f}" stroke="url(#rain-fall)" stroke-width="{r*.8:.2f}" stroke-linecap="round"/><circle r="{r:.2f}" transform="translate({x:.2f} {y:.2f}) scale(1 .86)" fill="url(#rain-liquid)"/></g>'
    return defs,body


def hail_art():
    defs='''<radialGradient id="hail-solid" cx="30%" cy="23%" r="80%"><stop stop-color="#f7faf8"/><stop offset=".28" stop-color="#e2edf0"/><stop offset=".55" stop-color="#becfd9"/><stop offset=".8" stop-color="#809cad"/><stop offset="1" stop-color="#526f89"/></radialGradient><radialGradient id="hail-frozen"><stop stop-color="#fcffff" stop-opacity=".68"/><stop offset=".7" stop-color="#f6ffff" stop-opacity=".16"/><stop offset="1" stop-color="#f0ffff" stop-opacity="0"/></radialGradient><linearGradient id="hail-trail" x1="1" y1="0" x2="0" y2="1"><stop stop-color="#b1cada" stop-opacity="0"/><stop offset="1" stop-color="#cbdfe9" stop-opacity=".35"/></linearGradient>'''
    body=''
    for x,y,size,rotation in [(70,60,.63,12),(149,54,.44,35),(199,94,.68,-21),(115,111,1,0),(51,134,.5,66),(165,164,.83,-48),(86,198,.65,27),(213,208,.43,68),(140,214,.36,11)]:
        body+=f'<path d="M{x+6} {y-36*size}l-4 {17*size}" stroke="url(#hail-trail)" stroke-width="2" stroke-linecap="round"/><g transform="translate({x} {y}) rotate({rotation}) scale({size})"><path d="M-18-12q5-10 15-10 7-4 14 3 12 1 12 13 5 9-3 15-2 12-14 13-8 6-17-2-12-1-12-12-5-13 5-20Z" fill="url(#hail-solid)"/><circle cx="-6" cy="-7" r="14" fill="url(#hail-frozen)"/><circle cx="8" cy="7" r="10" fill="url(#hail-frozen)" opacity=".5"/><path d="m-12-3 8-6 4 3 7-5m-7 5 2 9 7 5" stroke="#f6fbff" stroke-opacity=".38" stroke-width="1.2" stroke-linecap="round" fill="none"/><path d="M-16-4q-2-9 8-12" stroke="#fff" stroke-opacity=".55" stroke-width="1.5" fill="none" stroke-linecap="round"/></g>'
    return defs,body


def atmosphere_art():
    # Continuous altitude color transitions, rather than hard concentric shells.
    defs='''<radialGradient id="atmosphere-altitude" cx="50%" cy="50%" r="50%"><stop offset="0" stop-color="#345770"/><stop offset=".55" stop-color="#406780"/><stop offset=".63" stop-color="#8ac7df"/><stop offset=".69" stop-color="#9ed8ec"/><stop offset=".75" stop-color="#5ca5d4"/><stop offset=".83" stop-color="#477bb9" stop-opacity=".85"/><stop offset=".91" stop-color="#6c6d9f" stop-opacity=".48"/><stop offset="1" stop-color="#807da8" stop-opacity="0"/></radialGradient><radialGradient id="atmosphere-surface" cx="31%" cy="24%" r="84%"><stop stop-color="#88b4c2"/><stop offset=".28" stop-color="#4f7f98"/><stop offset=".6" stop-color="#294963"/><stop offset=".88" stop-color="#14283e"/><stop offset="1" stop-color="#2a4961"/></radialGradient><radialGradient id="atmosphere-light" cx="27%" cy="20%" r="78%"><stop stop-color="#ebf7ff" stop-opacity=".24"/><stop offset=".45" stop-color="#c3e7ff" stop-opacity=".04"/><stop offset="1" stop-color="#061328" stop-opacity=".23"/></radialGradient>'''
    body='''<circle cx="128" cy="128" r="105" fill="url(#atmosphere-altitude)"/><circle cx="128" cy="128" r="65" fill="url(#atmosphere-surface)"/><circle cx="128" cy="128" r="84" fill="url(#atmosphere-light)"/>'''
    return defs,body

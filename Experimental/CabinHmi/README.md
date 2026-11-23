# Cabin HMI prototypes

🧩 Three standalone 3D centre-display concepts for three different cars. All three use the Component Viewer's
**ControlVehicle**: the same body, tyre and rim facets from `Experimental/Shared/Models/` and the same `VEHICLE_SHEET` axle mounts.
🚩 Every name here is a **proposal awaiting approval**: the folder, the three HMI names and every menu label.
No integration into the Component Viewer or Asset Viewer has happened yet.

| HMI | Car / mood | Menus | Signature 3D moments |
|---|---|---|---|
| **A · Contour** | Rugged off-roader. Amethyst violet (engaged), verdant green (measured), black glass. | Terrain · Tyres · Tilt · Sensors · Lights · Vehicle | Topographic terrain shader with a scan sweep. Spring-damped drive-mode dial that sets locks, ride height and pressures. Solid / X-ray skin with glowing axle bars. Tilt pad poses the body. Proximity fans. |
| **B · Afterglow** | Ambient grand tourer. Dusk lilac and rose, frosted glass. | Horizon · Drive · Lights · Climate · Rest · Media | Dunes under a ringed planet. Wet mirror floor. Particle waves that follow the music. Three world presets. Glide preview with floor streaks. Cabin airflow coloured by each zone's temperature. Rest ring timer that dims the world. |
| **C · Nocturne** | Winged saloon. Night-lake navy glass; each menu brings its own light. | Home · Drive · Lights · Charge · Comfort · Media | Mirror lake under stars, moon glitter and mountains. Glass card row (route, assistant, weather, track). **Power current** Drive: the body turns to glass, and tank, engine, P2 e-motor, battery and rear axle glow inside it, joined by currents that pulse the way energy flows (electric, engine charging, e-boost, regen) with live kW, fuel, battery and split range. Aurora sky, charge bollard, flowing cable and floor dashes. Comfort: the car fades and two 3D seats rise from the lake. Each cushion slides fore/aft on its rail and each backrest tilts. The ring dot is the back-angle handle (drag it, or use the card), plus three seat memories, A/C zones, fan and airflow targets. 3D cover flow over the water (click a cover), beat rings on the lake. |

## Dashboard clusters (separate from the HMIs)
The instrument clusters sit on the dashboard, not in the centre display. Each is its own 1920 × 720 page in `Clusters/`,
with four modes (**Cruise · Eco · Sport · Race**) that change the layout, tint and drive style. All of them show **fuel** (litres, bar, range).

| Cluster | For | Source image | Modes add |
|---|---|---|---|
| **Ridge** (Ridge Tach) | Contour | Ford-GT bar tach | Cruise route/media · Eco coach · Sport engine/G · Race shift lights, lap timer, tyre heat |
| **Halo** (Halo Dial + G Compass) | Afterglow | Round speedo with G meter | Sky and arcs shift per mode; G Compass with max hold in Sport/Race |
| **Tide** (Tide Gauge) | Nocturne | RPM/turbo cluster | Eco and charge fans, shift lights and lap panel in Race |

`ClusterDeck.js` runs each cluster: a `Powertrain`, mode keys 1–4, ↑/↓ pedals, Q/E paddles, P R N D M, Demo and Ignition sweep.
📝 **Cabin link** (`Shared/CabinLink.js`, a BroadcastChannel per car): with a cluster open in another tab, the cluster drives
the car shown in its HMI (speed, gear, pedals, fuel), the HMI's light switches show up as the cluster's telltales, and the HMI's
selector moves the cluster's gear. Alone, each HMI's car stands parked.

## Windscreen HUD demo (Visor)
`Visor/Visor.html` is a separate game prototype, with no HMI and no dashboard. You sit in the ControlVehicle on an endless straight night pass
through snow and pines and move the mouse to look around the cockpit. The windows are clear. Every display element is light on the windscreen,
drawn on an upright virtual-image plane just inside the glass, square to the driver, like a real combiner. Each label is projected from the
driver's eye, so it sits exactly on its target.

| Event (one lap = 3.6 km, then it re-arms) | On the glass |
|---|---|
| Teammate ping (Kestrel) | Purple card; a light-pillar beacon with a tag and distance; an edge arrow when it is outside the glass |
| Teammate warning (Orca) | Card quoting the warning about the wildlife ahead |
| Porcupine crossing | Bracketed tag on the animal: amber at the roadside, red once it is in the path; distance, then CLEARED. Lane guides turn red, BRAKE flashes and the car brakes |
| Traffic signal | Countdown ring over the light, "x s to red": the ring turns green → amber → red as the time runs out (ahead of the lamp). Then "s to green", an amber or red stop bar at the line, and GO |
| Climb (9% grade) | Coolant rises: amber ENGINE card, then an ENGINE OVERHEAT banner. The car holds 40 km/h until it cools |
| Altitude | Readout climbs 2,400 → 4,200 m. Cabin O₂ card, then an OXYGEN LOW banner and a screen vignette. E turns on O₂ boost |
| Low fuel | FUEL LOW card; below 6% a FUEL CRITICAL banner |
| Station nearby | "Station ahead" suggestion with a floating pump tag. E routes in: chevrons paint the way into the bay, the car stops at the pump, fills up and rejoins |
| Ice | 0 °C ice-risk card |
| Same-direction traffic | Slow cars ahead in your lane get corner brackets with gap and speed. Adaptive cruise keeps 1.6 s + 8 m; emergency braking under 1.3 s to impact |
| Oncoming traffic | Platoons of cars in the far lane, with now and then a long gap. Faint brackets with speed. A car coming at you in your lane turns the box, guides and banner red (ONCOMING TRAFFIC · time to impact). With the autopilot on, it steers you home (RETURNING TO LANE) |
| Overtaking | Above the strip: OVERTAKE CLEAR / WAIT · ONCOMING n s / NO OVERTAKE · HAZARD AHEAD. With an OVERTAKE CLEAR card, E pulls out, passes at +15 km/h and tucks back in. It aborts if the oncoming window drops under 3.2 s |
| Collisions | Cars and pines are solid: red flash, COLLISION / HIT A TREE banner, knock-back. OFF ROAD banner and drag in the snow, rumble on the edge line |

Combiner strip (bottom): mini lane map with route, speed, autopilot set speed, next manoeuvre and distance, clock / outside °C / altitude,
fuel / coolant / O₂ gauges. The frame colour follows the worst live state.
**Steering.** A bicycle model with a 3.4 m wheelbase: A/D turn the front wheels (shown on the wheel and tyres). Lock is limited so cornering
never asks for more than about 6 m/s², so it is gentle at speed and tight when slow. Hands off with the autopilot on, a lane keeper steers to
the centre of whichever lane you are in. Lane guides sit on the real edges of the lane the car occupies; mid-change, both lanes show,
each faded by how much of the car is in it (far lane amber, red if a car is coming).

**Pings (squad-game style).** Hold the middle mouse button (or F) for the ping wheel and release on a wedge: Going here · Enemy · Danger · Supplies ·
Need fuel · Watching · Hold here · Regroup. A quick tap places the right ping for what is under the cursor (car → Enemy, porcupine → Danger,
station → Need fuel, signal → Watching, cache → Supplies, ground → Going here). Tap your own ping again to cancel it. Up to 3 of your own pings
exist at once, each lasting 30 s (16 s on a car). Each shows a diamond marker with pulse rings, distance and a light pillar; pings on cars follow
the car. Teammates (Kestrel, Orca) acknowledge (`ACK · KESTREL`) and ping things themselves.

**Sound (Web Audio, synthesised, no files).** Engine from rpm on a 6-speed box, tyre and wind noise, rumble strip, a cue per card level, BRAKE
alarm, a tick each signal second, a ping chime per call, wheel open and hover, a whoosh as cars pass (panned left or right), a crash, and the pump
while fuelling. Browsers start audio on the first click or key; M mutes.

Keys: mouse look · wheel zoom · W/S set speed (autopilot) or throttle / brake / reverse (manual) · A/D steer · Space brake · middle mouse / F ping ·
E accept / overtake · Q dismiss · C autopilot · M sound · H hide help.
`window.VisorCondition` is the test hook: `Jump`, `Look`, `Signal`, `Act`, `Dismiss`, `Press`, `Pace`, `Place`, `OpenWheel`, `CloseWheel`,
`Ping`, `Condition`, `Picture`, `Traffic`, `Pings`, `Sound`.

## Garage lobby configurator (Lobby)
`Lobby/Lobby.html` is a game prototype: the ControlVehicle parked in a procedural garage, like a car configurator. Each set is built from
code and a seed (no models or photos). Inspiration: `SourceImages/Lobby/Lobby-Ref-1…10`.

| Set (key 1–9) | Look | Garage options |
|---|---|---|
| Canopy Grove | Steel carport in a fern jungle, wet pad, warm uplights (refs 1, 2) | Mood (Overcast / Dusk / Night), Canopy (Black steel / Bronze / Concrete), Foliage, Wet ground, Garden uplights |
| Moss Stage | Black-box studio, moss island, fallen log, LED forest wall, light stands, top softbox (ref 3) | Forest wall (Spring / Autumn / Mist), Light stands, Fallen log, Haze, Top softbox |
| Lightbox | White room under a giant softbox (ref 4) | Floor (Graphite tile / Polished concrete / White epoxy), Walls, Softbox size, Accent line, Floor gloss |
| Race Hangar | Dark hall, a wall of round lamps, ceiling light bars, glossy tiles (ref 5) | Lamp colour, Lamp rows, Ceiling bars, Haze, Floor gloss |
| Monolith | Pale slabs on a mirror floor under an open sky (ref 6) | Time of day (Dawn / Noon / Dusk), Slabs, Floor gloss, Light streak |
| Shadow Canopy | Concrete overhang by the sea, hard sun through a roof cut (ref 7) | Sun (Morning / Noon / Golden), Concrete, Roof cut, Charging post |
| Pit Box | Race garage open to the pit lane and grandstand (refs 8, 9) | Session (Golden hour / Overcast / Night race), Garage number (over the opening, on the back wall and the stand board), Grandstand colours, Ceiling tubes, Wet floor |
| Photo Studio | Cyclorama island on dark concrete under a suspended softbox; fresnel back light, LED box, umbrellas, camera tripod, laptop table, flight cases (ref 10) | Overhead softbox, Cyc floor (White / Grey / Black), Walls (Concrete / Black / White), Fresnel back light, Crew gear |
| Workshop | Mechanics garage: two-post lift, tool chests, pegboard bench, tyre stacks, drums, compressor, tube lights (no reference) | Style (Clean shop / Old school / Night tuning), Lift height (raises the car), Tool chests, Tyre stacks, Roller door (Closed / Open to the yard) |

The seed (New seed / Shuffle all) changes the layout (plants, stones, slabs, wear) and keeps the options. Each set remembers its own options
and seed. The URL carries `?Set=…&Seed=…`.

**Panel tabs.** Garage: the set's options and seed. Car: six folding configurator groups. Paint (9 colours, Gloss / Metallic / Satin);
Wheels (rim colour, tyre colour, brake calipers); Lights (on/off, headlight colour, which also tints the spot beams, tail light colour,
including Smoked); Underglow (on/off, 7 colours, Steady / Breathe / Cycle: LED strips under the sills plus a glow pool and lights on the
floor); Components (rear wing, front splitter, side skirts, roof rack, roof light bar with a working spot); Drivetrain (Engine Inline-6 /
V8 / Electric, Driven wheels RWD / FWD / AWD, Gearbox Manual / Automatic) with game-scale figures. Light: a top-view light plot that is also the key-light dial (drag to swing the key around the car), key height,
key colour, key intensity, fills and practicals, reflections, exposure, reset. Camera: Set view, Front ¾, Side, Rear ¾, Top, Low, auto orbit,
lens (18–85 mm).

**Hotspots.** White dots ride on the car (Paint, Headlights, Tail lights, Wheels, Underglow, Aero, Roof, Drivetrain). Side dots follow
the camera to the near side and fade out when their part faces away. Clicking a dot flies the camera to that close-up and opens the
matching Car group; the top bar's ‹ Overview (or Esc) flies back. The Camera tab can switch the dots off.

**Drivetrain cutaway.** Changing a drivetrain option (or the Drivetrain dot / Show the cutaway) fades the body, wing and wheels into a
glass shell with feature edges. Inside it a holographic drivetrain is rebuilt for the choice: the engine block (inline-6 or V8, turned
sideways for FWD), the clutch disc and H-gate shifter (Manual) or the torque converter and selector (Automatic), the propshaft, the transfer
case and front diff (AWD), the rear diff and half shafts, the fuel tank and exhaust, or for Electric the battery modules, a motor on each
driven axle, reduction gears and HV cables. Moving dashes flow from the power source to the driven wheels, driven wheels get a green ring,
labels are pinned to the parts (stacked so they don't overlap) and a card shows power, torque, 0–100 km/h, top speed, weight and handling.
Overview returns the solid car; the bolt-on parts and the underglow hide during the cutaway.

**Camera.** Left drag orbits, right drag (or Shift + drag) pans, the wheel zooms. The camera stays inside each set's bounds (walls, ceiling,
floor). Keys: 1–9 sets · O auto orbit · H hide the interface · Esc overview.

📝 Reflections come from the set itself: after each rebuild the empty garage is rendered into a PMREM cube from the car's position, so the
paint picks up that garage's lamps, sky and walls. Glossy floors are semi-transparent over a `MirrorTwin`. A soft veil under the floor dims
the twin as the floor gets less glossy, and a contact shadow sits under the car.
`window.Lobby` is the test hook: `Ready`, `Use(Key, Options, Seed)`, `Look`, `Rig`, `View`, `Orbit`, `Tab`, `Spot(Id)`, `Overview`, `Spots`, `Bare`, `State`.

## Particle showroom (Filament)
`Filament/Filament.html` is a separate full-window demo of the particle effects seen on new cars' screens and in motion-graphics tools.
The ControlVehicle stands on a dark stage with ten looks (keys 1–9 and 0). Looks 7–10 follow reference stills (Follow-up 24) and hide the car by default:

| Look | What it shows |
|---|---|
| Light speed | Welcome / Sport-mode speed lines. Streaks rush past the parked car in a tunnel, and the lowest ones ride floor lanes. |
| Digital twin | Dust swirls in and builds the car nose-first from 160k points scattered on its real surface. A scan line sweeps it, then the wind blows it off (14 s loop). |
| Airflow | Wind-tunnel streamlines bend around the car's measured silhouette, bunch over the roof, turn red where the air piles up and tumble in the wake. Fill or smoke rakes. |
| Particular | Trapcode Particular-style: two emitters orbit the car. Particles age through a colour ramp, shrink over life and drift in a turbulence field. |
| Clone field | MoGraph cloner: a grid of clones around the car, driven by a sphere effector, a shockwave ring and a noise effector. Bars (scale) or dots (move). |
| Charging | Energy spirals down the cable, into the port and along the floor, then fills a 40-cell battery cell by cell (readout: %, kW, km). |
| Orbits | Light threads orbit a glass sphere (fresnel rim, highlight, faint feathers inside). The rings share one plane and are evenly spaced, widening slightly outward, so they never cross. Inner rings run faster (Kepler) and glow brighter; every 40th thread spirals in from far out; a few dots ride the rings. |
| Flora | Three flowers made of glittering particles: the petal edges glow, the petals slowly open and close, and motes drift off. Big leaves in front, fern fans behind and floating dust; depth of field turns the near and far parts into bokeh. |
| Vehicle transition | A jagged electric edge sweeps the car nose to tail, three times in a 17.5 s loop: the dotted dust twin becomes the painted car, the paint changes from graphite to lime, then the car streams away as particles. The real car body is cut and recoloured at the same edge, which glows on the paint. Everything runs on evenly spaced straight lanes: prismatic lines behind the edge, particles shot straight out ahead of it, thin electric wisps. Turbulence is unused here. |
| Fibre wave | A twisting sheet of optical fibres, narrow at one end and fanned out at the other. Light pulses run along it, with a bright, sparkling leading edge on a teal backdrop. |

**Controls.** Particles (5k–160k), speed, trail, size, glow, turbulence, depth of field (lens aperture, focused on the orbit target); 8 palettes (Ice, Ember, Aurora, Rosso, Mono, Lilac, Jade, Teal). Car body: Auto, Paint,
Ghost (black mirror), Glass (see-through with edge lines) or Hidden. Drag orbits, the wheel zooms. Keys: 1–9, 0 looks · Space pause · O auto orbit · H hide panels.

📝 Every particle is a camera-facing capsule drawn between a Head and a Tail point, so one instanced quad gives a dot, a comet, a speed line or a
clone bar. Motion is a function of the clock alone, with no simulation state: the Particular look re-integrates its 6-step turbulence path each
frame. Threads (orbits, fibres, fern fans) are runs of capsules with flat-cut joints: instance n draws segment n mod S of thread floor(n / S). Depth of field widens out-of-focus particles into flat bokeh discs with a brighter rim. The random numbers come from a PCG integer hash, because sin-based hashes clump at large instance counts. All 160k particles are one draw call per look. Additive HDR (half-float) feeds a dual-filter bloom, then ACES, a vignette and grain (`BloomChain`).
URL options for captures: `?look=Airflow&time=6&variant=1&palette=Ice&car=Ghost&count=80000&still=1&ui=0&capture=1`. `window.Filament` is the test
hook: `SetLook(Key)`, `Seek(Seconds)`, `Settings`.

## Particle engine location
The particle engine (`FilamentRig`, `FilamentLooks`, `FilamentFibres`, `SurfaceScatter`, `BloomChain`) now lives in the standalone particle editor
app, `Experimental/ParticleEditor/Source/Particles/`. This showroom imports it from there and runs one look at a time. The editor, with its stacked
layers and Frontier Editor layout, is that separate app; see `Experimental/ParticleEditor/README.md`.

## Vehicle sound library (Acoustics)
`Acoustics/Acoustics.html` is a sound bench: drive a Ferrari LaFerrari, a Nissan GT-R NISMO, a Porsche Taycan Turbo S or an original Next-Gen
racer (Jackson Storm style) live (keyboard or on-screen pedals), bolt any of 10 turbos and superchargers onto the petrol cars, switch the
road surface and the microphone (Chase / Cabin), hit kerbs and potholes, fire light, cabin and impact cues, and play the pre-rendered reel.
**No recordings**: every sound is synthesized sample by sample from a physical model. The same classes run live in an AudioWorklet and offline
in Node (`Acoustics/Offline/RenderReel.mjs` → MP3s).

| Sound | How it is made |
|---|---|
| LaFerrari V12 | Crank angle advances with rpm; each of 12 cylinders drops a blowdown pulse into its bank (65° V, uneven 65°/55° gaps → 6th-order firing plus 3rd-order bank rasp). Pulses ring header and tail-pipe waveguides with an X-pipe cross-feed, a muffler with bypass valves and body formants. Intake airbox, runner and howl resonances, valvetrain shimmer, gear whine, HY-KERS motor and inverter whine, starter with compression load. Follow-up 11 "chatter": every cylinder has its own pulse shape, level and timing lean, cycle-to-cycle jitter (strong at idle, evening out up high where tuned headers balance the banks) and lumpy overrun burble, so the half orders and the firing sequence are audible; valve-closing and injector ticks ring small metal modes each event, a valvetrain shimmer rides on top, the gearbox whines per gear and clunks on each shift, and the chase mic gets early reflections. |
| GT-R NISMO V6 | VR38DETT, 3.8 L 60° V6, even 120° firing (3rd order, rpm/20), 600 PS at 6800, 652 Nm 3600–5600, 7100 limiter, the real GR6 ratios and 3.700 final drive. The twin IHI turbos add detuned twin whistles that rise with shaft speed, a spool whoosh, a blow-off vent on a sharp lift, compressor surge flutter and the wastegate; the turbines muffle the exhaust pulses. Boost builds with lag in `TractionIntegrator` (`BoostTarget`), the AWD launch puts 95 % of the weight on the driven tyres (0–100 2.9 s), the DCT shifts and clunks. |
| Drive model | `TractionIntegrator`: torque curve (700 Nm at 6750), inertia and friction, 7-speed DCT (45 ms ignition-cut upshifts, blipped downshifts), launch slip, 9250 rpm limiter, traction control, brakes with regeneration, drag. Overrun and cut afterfire pops. |
| Surfaces | Asphalt, wet, gravel, dirt, grass, cobbles, rumble strip: tread-block tone, road roar, wind; water sizzle and splashes; discrete stone hits and underbody pings; periodic impacts through suspension and body modes. |
| Tyres | Stick-slip squeal oscillator plus tread resonances with wandering pitch; lock-up scrub; wheelspin howl and smoke hiss; loose surfaces scrub instead of squealing. |
| Forced induction | `ForcedInduction.js`: 11 inducers (10 plus the Demon's own blower) you can bolt onto any combustion car (they replace its stock induction; `InductedProfile` re-tunes the boost curve and lag, `InductionVoice` voices it). Turbos: small single (quick spool, bright whistle, soft recirculating blow-off), big single (late, violent boost, deep whoosh, loud blow-off), parallel twins (two detuned whistles beating), no blow-off valve (compressor surge "stu-tu-tu" on every lift), variable geometry (diesel-style low-rpm boost, whistle sweeping with the vanes), electric assist (a motor pre-spins the shaft: no lag, electric whine, harvest whine on lift). Superchargers are geared to the crank, so their whine is an exact order of engine rpm: Roots (3-lobe, chugging pocket pulse), twisted 4-lobe (high smooth scream, bypass valve on the cruise), twin-screw (raspy harmonic-rich scream, timing-gear whine), centrifugal (belt-driven impeller, whine and boost rising with rpm squared). Inducer levels sit about 8–12 dB under the engine. |
| Dodge Demon | `Demon` (`PulseTrainEngine.js`): Challenger SRT Demon, supercharged 6.2 L HEMI cross-plane V8 (the real car is supercharged, not turbocharged). `BankAngles` fires each bank unevenly (firing order 1-8-4-3-6-5-7-2), the source of the V8 burble. 840 hp at 6300, 1044 Nm at 4500, 6500 limiter, TorqueFlite 8-speed ratios, TransBrake (hold B: staged at 2350 rpm, launch on release). Its stock 2.7 L twin-screw blower (`DemonScrew`, `Stock`) plays unless an inducer is swapped in. |
| 918 Spyder | `Spyder918`: the hybrid from Porsche's 2020 Super Bowl advert "The Heist". 4.6 L flat-plane V8 from the RS Spyder racer (each bank fires evenly every 180°), 608 hp at 8600, 9150 limiter, short top-exit pipes. 115 kW hybrid module on the crank (whine locked to rpm), 95 kW front-axle motor (`Motor.Front`: own gear, power-limited, decoupled at 235 km/h, whine locked to wheel speed), 7-speed PDK. |
| Agera RS | `AgeraRs` (Follow-up 22; was `AgeraR`): 5.0 L twin-turbo cross-plane V8 (90° crank), 1160 hp at 7800, 1280 Nm at 4100, redline 8250. Earlier notes: 5.0 L twin-turbo, 1200 Nm at 4100, 1140 hp at 7100 on E85, 7500 limiter, 7-speed DCT, rear drive, loud dump valve on every lift. Gear ratios chosen for its ~440 km/h (not published). |
| Mustangs | Follow-up 18, three Mustang sounds. `MustangGt`: 2024 Mustang GT, 5.0 L Coyote cross-plane V8 (uneven per-bank firing, the burble), 486 hp at 7250, 567 Nm at 4900, 7500 limiter, Getrag MT82 6-speed manual (3.23…0.62, 3.55 final). `Gt350`: Shelby GT350, 5.2 L Voodoo **flat-plane** V8 (each bank fires evenly, so no burble; a scream to 8250), 526 hp at 7500, 582 Nm at 4750, Tremec TR-3160 (3.25…0.63, 3.73 final). `MustangGt3`: the LMGT3 / IMSA race car, 5.4 L race Coyote V8, ~550 hp under BoP, 7800 limiter, Xtrac 6-speed sequential (typical ratios; not published), side-exit pipes, straight-cut whine, 1289 kg. |
| Ligier JS P320 | `LigierJsP320`: the LMP3 prototype (not LMGT3) with the Nissan VK56 5.6 L cross-plane V8 (firing 1-8-7-3-6-5-4-2), ~460 hp, 7500 limiter (race tune not published), Xtrac 6-speed sequential with paddles (typical ratios), straight-cut whine, 950 kg, mid-engine. |
| Rotating mass | `Drive.Rotating` (opt-in, the four Follow-up 18 cars): wheels, driveline and flywheel add effective mass, mostly in the low gears (`1.04 + 0.0025 × overall ratio²`, a textbook approximation). |
| Turbo lag | `TractionIntegrator.Turbo`: a shaft model rather than a fixed delay. Exhaust energy (rpm × load × boost) spins up a turbine with inertia; pressure goes with shaft speed squared, capped by the wastegate; lifting dumps boost but the shaft coasts. So the lag is long from low rpm, short near the limiter, and short on a quick re-apply. The whistle follows the same shaft. The Turbo lag slider (0 = off to ×3) and a tip-in stopwatch sit under the induction pads. Superchargers have no lag. |
| Taycan Turbo S | `ElectricDrive.js` (`TaycanTurboS`, `ElectricIntegrator`, `DriveUnit`): two permanent-magnet motors (front single-speed 8.05:1, rear two-speed 15.5 / 8.05:1, 16 000 rpm, 800 V), 560 kW overboost, 1050 Nm. Every tone is an order of the motor shaft (pole-pair torque ripple, slot order, gear mesh), so pitches follow real shaft speed; inverter switching tones with sidebands, rotor windage, rear-gearbox shift thunk with a torque dip (up at 28–100 km/h by load, kick-down to 1st below 60 km/h), regenerative braking (the whine keeps singing while slowing), ready chime and an optional synthetic sport-sound layer (`Sport`, like the speaker sound packs). Model 0–100 2.83 s, 0–200 9.86 s, top 259 km/h. |
| Next-Gen racer | `NextGenRacer`: an **original** design in the spirit of Jackson Storm from Cars 3 (the film describes an electric-sounding whir with a gearbox). A twin-tone electric whir locked to the motor dominates, with a low V8 undertone, straight-cut gear whine on the overrun and a 6-speed sequential box that shifts up at 17 000 rpm (each shift drops the pitch) and blips down on the brakes. Nothing is taken from the film audio. Model 0–100 3.43 s, 0–200 7.27 s, top 338 km/h. |
| Electric aircraft | `ElectricAviation.js`: `PropellerVoice` (blade-pass tone and harmonics that get richer as the tips approach Mach 1, broadband blade noise swishing at the blade rate, blade mismatch, gusts) plus motor whine and inverter tones; `Fly` moves the aircraft past a mic with Doppler, distance, air absorption and a ground bounce. Airframes (typical figures): 2-seat trainer (2-blade prop), speed-record racer (3-blade, tips near Mach 0.9), 6-rotor eVTOL air taxi (5-blade rotors at slightly different speeds, so they beat). On the bench these play as pre-rendered clips. |
| Lights | Headlamp switch detents, lamp relay, xenon igniter snap with arc flutter and ballast whine, LED welcome ticks with levelling motors, indicator tick/tock relay (loop). |
| Brakes | `BrakeRotor` (`ChassisVoices.js`): pad friction ringing two rotors' modes (louder with pressure × sliding speed), low-speed light-pressure squeal that comes and goes, stick-slip groan creeping to a stop, ABS at ~14 Hz with pump buzz and valve clicks (the integrator now models ABS). |
| Suspension | `SuspensionTravel`: a quarter-car (body, wheel, spring, damper, tyre) integrated every sample under the road profile of each surface plus weight transfer; damper oil hiss, force thuds through body modes, bump-stop and top-out clunks, bushing creaks, trim rattles, and an extra slam from a kerb face or a pothole's far lip. |
| Impacts | `ImpactFoley.js`: explosion (Friedlander blast overpressure, falling sub thump, rough fireball roar darkening as it burns out, debris and a bouncing fragment, echoes off distant surfaces, air absorption with distance), shockwave (N-wave double crack, air rush, rattles), EMP pulse (charge whine, discharge slap and sub drop, arcing, electronics winding down, ear ring), sonic blast (two close tones beating over an infrasonic body), side window shatter (tempered glass crazing into hundreds of cubes), glass breaking (ringing shards breaking away, landing, scattering, bouncing), metal crash (impact, 34 buckling folds sinking in pitch, lamps bursting, scrape, hubcap, stick-slip settling creaks, coolant hiss). |
| Weapons | `Ballistics.js`, hold to fire. Energy LMG in the style of Apex Legends' Devotion: the fire rate climbs 5 → 15 rounds/s over 1.75 s while held (Turbocharger hop-up: 6.8 → 15 in 0.85 s, with a turbine whistle and charge hiss), 44-round magazine, dry click when empty. Follow-up 14 voices it as a real full-power LMG: long muzzle blast, low-mid boom (barrel and air), supersonic crack, steel bolt carrier, rolling outdoor report, brass falling; the spin drive is a geared motor (mesh grind, barrel clatter) and the energy ammo only adds a faint arc crackle (the old pitched zap is gone). Also LMG (640/min, belt links, brass), SMG (900/min), sniper (heavy blast, valley echo, bolt cycle), turret (servo traverse, lock-on pips, burst), 120 mm tank cannon (blast wave, chest thump, breech, case clang) and 30 mm autocannon. Live shots are scheduled on the audio clock from `FireRate`; the spin loop follows the rate. |
| More guns | Follow-up 19: .50 cal HMG (M2-style, 550 rounds/min), AK-47 (600/min, 30 rounds), M4 carbine (800/min), minigun (M134-style, spins up in 0.5 s to 3000/min, motor whine), MG 42 (1200/min buzzsaw), 12-gauge pump shotgun (pump cycle after every shot), 9 mm pistol (semi-auto), .50 anti-materiel rifle (muzzle-brake side blasts). All hold to fire, all drop their own brass. |
| War | `WarFoley.js`: explosion in sand (30 m and 350 m: whump, sand column, sand raining back), mortar launch, incoming mortar (Doppler whistle from the real flight path), frag grenade, RPG, artillery barrage 1–5 km, military helicopter fly-over, air-raid siren, tank passing, shell shock (muffled hearing, ringing, heartbeat), battlefield bed. |
| Bullets | Supersonic crack (N-wave) with the muzzle report arriving later by distance (400 m → 0.67 s), subsonic whizz-by (Doppler, panned), ricochets (strike + tumbling whine), and impacts on sand, dirt, rock, concrete, wood, metal, glass and water (`Hit<Material>`). |
| Shells | `Casings.js`: spent cases as thin tubes that ring their own modes (9 mm ≈ 9.4 kHz down to a 120 mm stub ≈ 520 Hz), land mouth-first (ring) or head-first (click), bounce with the floor's restitution using real drop physics (so the gaps shrink like a real bounce series), then roll with a rim tick per turn and a rough-floor rattle. Floors: concrete, wooden deck, steel (plate modes ring), dirt (kills the ring, crunches). 10 cues (rifle, pistol, shotgun, .50, 30 mm, 120 mm, four floors, an LMG brass-and-links rain piling up); every gun's live shots drop brass on concrete. |
| Factory | `FactoryFoley.js`: conveyor line (loop: 4-pole motor at 24 Hz, 23-tooth reducer mesh at 552 Hz, roller rumble, drive chain, a dry roller bearing, belt splice clacks, boxes over the transfer plate), factory floor (loop: hall rumble, mains hum, far motor, clanks, air blasts, forklift beeps), blacksmith hammering and quench, angle grinder (universal motor ~11 000 rpm, bevel mesh, commutator, load sag, screech, sparks), hydraulic stamping press, MIG welding (short-circuit crackle, wire feed, gas), air impact wrench, steel pipes dropped (free-free beam modes). Every cue sits in a Freeverb-style factory hall; loop lengths hold whole cycles of their machine tones and the tail is cross-folded over the start. |
| Creaks | Stick-slip trains ringing structural modes: steel beam under load, heavy steel door (dry hinge, slam, latch), wooden floorboards, chain hoist (pawl ratchet, links, beam creak). |
| Sparks and electronics | Spark shower, short circuit (arc, bang, fizz, breaker trip, cooling ticks), high-voltage climbing arc (100 Hz buzz, three climbs), loose wire, transformer hum (loop: 100 Hz magnetostriction, loose lamination, fan), contactor and relay bank (contact bounce), robot arm servos and gripper, fluorescent tube starting, coil whine under a changing load. |
| Road noise | The roar at speed is the tyres on the road (`SurfaceRoll` road layer); wind is 13–30 dB under it. Mix has a Road noise switch (M), a Road roar slider (40 % by default) and a separate Wind slider (80 %); above 150 km/h the roar grows ~3 dB per doubling instead of ~8 (a listening choice). |
| Cabin | Unlock actuators and mirror motors, butterfly door open (latch, seal pop, gas struts), door close thud, seatbelt ratchet and latch, belt chime, start button with wake chime, gear paddle, window, wipers, climate fan (loop). |

Keys: W throttle · S brake · A D steer · Q E paddles · Space handbrake · Enter start / stop · V car (4) · I induction · K kerb · P pothole · F fire (picked weapon) · R Turbocharger · C mic · G gearbox ·
T traction · M road noise on/off · 1–7 surface.
`window.Bench` is the test hook: `Wake`, `Command(Name)`, `Hold(Control, Amount)`, `Cue(Name)`, `Car(Name)`, `Kick(Kerb|Pothole)`, `Surface(Name)`,
`Readout`, `RenderOffline(Seconds, Car, Inducer, Gear)`, `Fire(Weapon, Seconds)` → `{ Fired, Seconds, Turbo }`, `Turbo(On)`,
`Induction(Name | null)`, `Sport(On)`, `Fly(Cue)` → seconds, `Mix(Name, Level)`, `RoadNoise(On)`, `Levels()`; `RenderOffline` takes a fifth
argument, a mix (e.g. `{ Engine: 0 }` to hear the road layer alone). If the worklet ever throws, it goes quiet and posts `{ Fault }` (logged as a console error).
Re-render the reel: `cd Experimental/CabinHmi/Acoustics/Offline && npm install` once (lamejs), then
`node Experimental/CabinHmi/Acoustics/Offline/RenderReel.mjs [Scene …] [--wav]` (about 450 s for all 100 scenes; pass scene names to render just those).

## Lights
Each HMI has its own **Lights** menu on the shared `LampRig`. These are real lamps on the 3D car.

| HMI | Lamps |
|---|---|
| Contour | Off/Auto/Park/Low/High, front fog, rear fog, auto high, roof light bar, underbody, hazard, indicators |
| Afterglow | Beams, fogs, auto high, welcome light, tail ribbon, hazard, indicators |
| Nocturne | Beams, fogs, auto high, wing blade, ground halo, hazard, indicators, Moonrise greeting sequence |

📝 `Lucent/` is the first HMI C attempt, which the owner rejected. It stays on disk for reference only and is not on the launcher.

## Run
`python3 Experimental/CabinHmi/ServeCabinHmi.py 8097` serves the repository root and redirects `/` to the launcher
(`Experimental/CabinHmi/index.html`: HMI, cluster, Visor, Lobby, Filament and Acoustics cards). Any static server rooted at the repository also works; open:
- `/Experimental/CabinHmi/Contour/Contour.html`
- `/Experimental/CabinHmi/Afterglow/Afterglow.html`
- `/Experimental/CabinHmi/Nocturne/Nocturne.html`
- `/Experimental/CabinHmi/Clusters/{Ridge/Ridge.html, Halo/Halo.html, Tide/Tide.html}`
- `/Experimental/CabinHmi/Visor/Visor.html` (full window, not a fixed panel)
- `/Experimental/CabinHmi/Lobby/Lobby.html` (full window)
- `/Experimental/CabinHmi/Filament/Filament.html` (full window)
- `/Experimental/CabinHmi/Acoustics/Acoustics.html` (sound bench; click once to wake the sound)

Each display is a fixed 1600 × 900 panel, scaled to fit the window. Drag to orbit and scroll to zoom.
`window.HmiCondition` (HMIs) and `window.ClusterCondition` (clusters: `SetMode`, `SetDemo`) expose state and actions for tests.

## Layout
| Path | Holds |
|---|---|
| `Shared/VehicleAssembly.js` | Facet decode → ControlVehicle (Y-up, ground at 0). `DefaultFinish`, `PanelEnvironment` (PMREM panel IBL), `EaseColour`, `MirrorTwin` (reflective floors without a Reflector). |
| `Shared/DisplayKit.js` / `.css` | `FitDisplay`, `CameraOrbit` (eased presets, drift, pointer parallax), `ProjectAnchor`, `RollNumber`, `BindRipples`, `ClockText`, `RunLoop`. DM Sans faces and the press ripple. |
| `Shared/Powertrain.js` | Gear ratios, torque, drag, auto shifts, boost, fuel/charge, G forces, demo drive, ignition sweep; `BindPowertrainKeys`, `BindHold`. |
| `Shared/LampRig.js` | Real lamps on the car (beams with cones and pools, fogs, indicators, brake, reverse, extras); `LampGlyphs`, `BindLampControls`, `SyncTelltales`, `LampEcho` (lamp state without a car, for the clusters). |
| `Shared/CabinLink.js` | `OpenLink(Car)`: Send / Fresh / On over a BroadcastChannel between a cluster and its HMI. |
| `Clusters/ClusterDeck.js` / `.css` | Shared cluster runtime and the 1920 × 720 dash frame; `Ridge/`, `Halo/`, `Tide/` hold each page and its gauge. |
| `Contour/`, `Afterglow/`, `Nocturne/` | One HTML + CSS + JS per HMI. |
| `Visor/Visor.js` | HUD demo: cockpit, head look, driving and autopilot, event script, glass projection and composition. |
| `Visor/VisorCourse.js` | Endless road, snow, pines, reflector posts, sky; porcupine, traffic signal, Northpass station, teammate beacon. |
| `Visor/VisorGlass.js` | Canvas painter for the windscreen: tags, ping marks, car brackets, signal dial, guides, chevrons, edge arrows, advisory, combiner, cards, banner. |
| `Visor/VisorTraffic.js` | `MakeTraffic`: cloned ControlVehicles in both lanes, with car following, stop points, head and tail lamps, and light pools. |
| `Visor/VisorPing.js` | `PingCalls` (8 calls) and `PingWheel`, the radial ping menu on its own canvas. |
| `Visor/VisorAudio.js` | `SoundDesk`: synthesised engine, road, cue and ping sounds. |
| `Lobby/Lobby.js` | Garage configurator page: renderer, set environment capture, car options, orbit camera, key-light dial and light plot, panel. |
| `Lobby/LobbyCar.js` | Configurator car parts: `MountParts` (underglow, splitter, skirts, roof rack, light bar), `XraySkin` (glass body shell), `MountDrivetrain` (holographic cutaway with flow shader and tags), `DriveFigures`, `DriveNames`. |
| `Lobby/LobbySets.js` | The nine sets. Each one is `{Key, Name, Tagline, Swatch, Reference, Controls, Build(Options, Random)}`. |
| `Acoustics/SignalKit.js` | DSP pieces used by every sound: `SeededNoise`, `OnePole`, `Biquad`, `Resonator`, `ModeBank`, `DcBlock`, `DelayLine`, `PipeSection` (waveguide), `Slew`, `Wander`, `NarrowTone`, `Blep`, `PolySaw` (Follow-up 20), `SoftClip`, `PanGains`, `Strike` (burst into tuned modes). |
| `Acoustics/PulseTrainEngine.js` | `PulseTrainEngine` (live engine voice; `Pickup` Chase / Cabin / Dry), the `LaFerrari` and `GtrNismo` profiles, `Profiles`, `FullTorque`, `BoostTarget`. |
| `Acoustics/TractionIntegrator.js` | Longitudinal car model feeding the voices (any profile): start sequence, DCT shifts with gear and clunk counters, turbo boost with lag, limiter, launch, traction, brakes with ABS, cornering slip, acceleration for the chassis voices. |
| `Acoustics/ChassisVoices.js` | `BrakeRotor` (pad roar, squeal, groan, ABS) and `SuspensionTravel` (quarter-car: dampers, thuds, bump stops, bushings, rattles, kerb / pothole). |
| `Acoustics/ImpactFoley.js` | `Cues` (Explosion, Shockwave, EmpPulse, SonicBlast, WindowShatter, GlassBreak, MetalCrash), `RenderCue` (peak-normalized; `Distance` option); exports its layer helpers (Overpressure, Thump, Roar, Scatter, Bounce, Whine, Creak, Arcing, Echoes, Distant, Settle). |
| `Acoustics/Ballistics.js` | `Weapons` (EnergyLmg, Lmg, Smg, Turret, AutoCannon, and from Follow-up 19 Fifty, Ak47, M4, Minigun, Mg42, Shotgun, Pistol, AntiMateriel recipes; `Pump` and `Brake` layers; `Boom`, `Crack`, `Arc`, `Tail`, `Level` layers; `Brass` names a `Casings` case), 10 `Shells*` cues, `FireRate(Name, Held, Turbo)`, `Materials`, `Cues` (8 weapons, 3 bullet cues, 8 `Hit<Material>`), `RenderCue`, `RenderShot` (one shot for the live trigger), `RenderMotor` (seamless spin loop at top rate). |
| `Acoustics/ContactPatch.js` | `Surfaces` (7 recipes), `SurfaceRoll` (rolling, road texture, wind; separate `RoadLevel` and `WindLevel`), `TyreSqueal` (squeal, skid, wheelspin). |
| `Acoustics/WarFoley.js` | Follow-up 19. `Cues` (12 War cues: SandBlast, SandBlastFar, MortarLaunch, MortarIncoming, Grenade, RpgRocket, Barrage, Helicopter, AirRaidSiren, TankDriveBy, Deafened, Battlefield), `RenderCue`; internal `Flight` (moving source: Doppler and range from the path), `AirCut`, `Afar` (push a sound away), `Blast`, `Clods`, `DrawSand`, `DrawBattle`. |
| `Acoustics/FactoryFoley.js` | `Cues` (8 factory, 4 creak, 9 sparks/electronics), `RenderCue` (hall reverb, loop fold, peak normalize), `Hall` (Freeverb-style reverb); internal layers `Air`, `Motor`, `Hum`, `Crackle`. |
| `Acoustics/CabinFoley.js` | `Cues` (5 light cues, 11 cabin cues), `RenderCue`. |
| `Acoustics/Detonation.js` | Follow-up 20. `Fft`, `Convolve`, `Spaces` / `SpaceIr` / `Place` (generated place reverbs), `Limit`, `Pulse` (blast wave), `Rumble`, `Detonate` (charge- and ground-scaled explosion). |
| `Acoustics/Casings.js` | `Shells` (Pistol, Rifle556, Rifle762, Fifty, Shotgun, Cannon30, TankStub, Link), `Floors` (Concrete, Wood, Metal, Dirt), `DropCasing`, `CasingRain`. |
| `Acoustics/ForcedInduction.js` | `Inducers` (11 turbo and supercharger models: SmallSingle, BigSingle, TwinTurbo, Flutter, Vgt, ElectricTurbo, Roots, TwistedLobe, TwinScrew, DemonScrew, Centrifugal; Follow-up 20 parts `Compressor`, `BlowOff`, `Surge`), `InductedProfile(Base, Name)`, `InductionVoice`. |
| `Acoustics/ElectricDrive.js` | `TaycanTurboS`, `NextGenRacer`, `ElectricCars`, `ElectricIntegrator` (motor torque/power limits, multi-speed box, regen), `DriveUnit` (motors, inverter, gears, windage, whir, undertone, sport sound, chime). |
| `Acoustics/ElectricAviation.js` | `Airframes` (ElectricTrainer, ElectricRacer, AirTaxi), `PropellerVoice`, `Cues` (TrainerRunUp, TrainerTakeoff, RacerFlyover, AirTaxiHover), `RenderCue`. |
| `Acoustics/VoiceRackWorklet.js` | AudioWorklet `voice-rack`: runs the car live on the audio thread (petrol with an optional inducer, or electric), posts telemetry, reports a `Fault` instead of dying silently. |
| `Acoustics/Acoustics.html` / `.css` / `.js` | The bench page: tachometer, pedals, paddles, surfaces, cue pads, mix, reel players. |
| `Acoustics/Offline/RenderReel.mjs` | Scripts the 101 reel scenes, writes MP3s (and WAVs with `--wav`), spectrogram plates and levels. |
| `Acoustics/Offline/SpectrumTools.mjs` | WAV and MP3 (`WriteMp3`, lamejs) writers, FFT, spectra, hand-rolled PNG plates (spectrogram + waveform). |
| `Filament/Filament.js` | Particle showroom page over `ParticleEditor/Source/Particles/FilamentRig.js`: panel, keys, one look at a time, `window.Filament`. |
| `Lobby/LobbyKit.js` | `Seeded` random, canvas-painted textures (tile, concrete, asphalt, moss, bark, shutter, sky, sign, forest wall), beams, halos, fronds, leaves, `Scatter` (instanced), `Release`. |

## Known limits
⚠️ The model has no door, hood or tailgate meshes. Lids are therefore shown as hinged outline plates (Lucent).
Locks are shown as callouts and axle bars (Contour).
⚠️ Sensor distances, speed, charge and music are synthetic.
⚠️ Seats are sculpted stand-ins (cushion + backrest), not cabin geometry; slide and angle are shown, not physically simulated.
⚠️ Power-current kW, battery drain and fuel flow are display-scale, not a vehicle model.
⚠️ Visor: speeds, coolant, fuel burn and cabin O₂ are game-scale. The road is straight by design, and the porcupine and signal are scripted per lap.
⚠️ Visor traffic is two-lane only (no junctions). Other cars do not overtake, and they swerve for nothing but stop points. A software renderer (headless) draws about 1 fps with traffic; captures use `Pace(2)`.
⚠️ Lobby: the camera is clamped to a box, not collided with props, so it can pass through a pillar or plant. Rebuilding a set takes a short moment (hidden by a fade on set switches).
⚠️ Filament: the airflow is a look, not CFD. Streamlines are pushed around a boxy outline of the measured silhouette, so the colours only suggest pressure and speed. The charge figures are display-scale. The headless software renderer draws the 160k-particle looks slowly, so captures render single frames (`capture=1`).
⚠️ Acoustics: synthesized, not sampled. Without reference recordings neither car is A/B-matched to the real thing: the LaFerrari and GT-R voices are tuned to published figures (power, torque, ratios, redlines) and by measurement (firing order lands on rpm/10 and rpm/20; spectrogram checks), so they read as "a high-revving V12 with hybrid whine" and "a twin-turbo V6 with whistle and blow-off" rather than forensic copies. Pipe lengths, formants, turbo whistle pitches and the impact recipes are informed guesses. The chase mic now has a few early reflections, but there is still no full reverb or room model; the impacts' outdoor echoes are simple taps plus a comb wash. Explosions, EMP and sonic blast are cinematic designs (real EMPs are silent; a real sonic weapon is mostly inaudible). Live audio needs a click first (browser rule). The weapons are stylised originals: the energy LMG borrows only the Devotion's published fire-rate behaviour (5 → 15 rounds/s, 1.75 s, 0.85 s with Turbocharger, 44 rounds), not its audio. Gunshots have no real muzzle blast level (they are normalized), and impacts are short procedural recipes. The reel is 192 kbps MP3 (7.1 MB).
⚠️ Acoustics power sounds: the inducer and aircraft figures are typical values, not manufacturer data; Taycan pole pairs, gear teeth and inverter carrier are informed guesses. The Next-Gen racer is an original sound in the spirit of Jackson Storm, not film audio. An inducer replaces the car's stock induction. Aircraft are pre-rendered clips on the bench (too heavy to synthesize on demand).
⚠️ The Afterglow HMI still shows its old EV battery pill, while its Halo cluster runs a 3.0 petrol car.

Evidence (screenshots and the capture script) lives in `Exhibits/CabinHmi/`.

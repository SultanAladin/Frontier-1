// PULSE TRAIN ENGINE
// 🧩 Engine sound from first principles: a crank angle advances with engine speed, every cylinder that fires drops an
//    exhaust blowdown pulse into its bank's header, and each bank's pulse train rings through a header pipe (or a
//    turbine), a cross-feed, a tail pipe, the muffler (with bypass valves) and the body's formants. Intake pulses ring an
//    airbox and runner resonance; valvetrain and injector ticks, gear whine and shift clunks, turbo spool, whistle,
//    blow-off and surge flutter, the hybrid motor and the starter are added on top, then a few early reflections.
// 📝 The engine order content is not drawn: it falls out of the firing pattern. A 65° V12 with shared crank pins fires
//    with uneven 65° / 55° gaps (6th order plus a 3rd-order bank rasp); an even-fire 60° V6 alternates banks every 120°
//    (3rd order, each bank at 1.5). Every cylinder has its own strength, timing and pulse shape (its header runner), and
//    each combustion varies cycle to cycle, so the half orders that make an engine sound alive are present.

import { Tau, Clamp, Mix, Smoothstep, SeededNoise, OnePole, Biquad, ModeBank, DcBlock, DelayLine, PipeSection, Slew, PanGains, SoftClip } from './SignalKit.js';
import { Compressor, BlowOff, Surge } from './ForcedInduction.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROFILES
//------------------------------------------------------------------------------------------------------------------------

/**
 * Ferrari LaFerrari: F140FE 6.3 L naturally aspirated 65° V12, direct injection, HY-KERS motor on the gearbox, 7-speed
 * dual-clutch. Idle about 1000 rpm, limiter 9250 rpm, 800 cv at 9000 rpm, 700 Nm at 6750 rpm, ~270 Nm electric.
 * Pipe lengths, formants and runner spread are tuned by ear and measurement to a high, clean but raspy wail.
 */
export const LaFerrari = {
    Name: 'Ferrari LaFerrari', Code: 'F140FE V12 + HY-KERS',
    Cylinders: 12, BankOffset: 65, Idle: 1000, ColdIdle: 1320, Limiter: 9250, Redline: 9000,
    Header: [0.62, 0.66], Tail: [1.85, 1.98], GasSpeed: 560, CrossFeed: 0.18,
    Formants: [[170, 1.1, 2.5], [560, 1.6, 1.5], [1350, 2.2, 3.5], [2650, 2.6, 4.5], [4300, 3, 1.5]],
    ValveRpm: 3300, Intake: { Box: 118, Runner: 780, Howl: 1700 },
    Spread: 0.17, Lean: 2.6, Shape: 0.22, Jitter: [0.26, 0.08], Burble: 0.75,
    Valvetrain: 0.055, Injector: 0.03, Space: 0.24,
    Torque: [[800, 380], [2000, 470], [3000, 550], [4000, 600], [5000, 645], [6000, 685], [6750, 700], [8000, 690], [9000, 650], [9400, 560]],
    Motor: { Order: 8, Carrier: 9100, Torque: 270 }, Turbo: null,
    Gearbox: { Teeth: [31, 28, 26, 24, 23, 22, 21], Whine: 0.005, Clunk: 0.35, Cut: 0.045, Swap: 0.07 },
    Ratios: [3.08, 2.19, 1.63, 1.29, 1.03, 0.84, 0.69], Final: 4.44, TyreRadius: 0.357, Mass: 1585 + 75, DragArea: 0.82,
    Drive: { Share: 0.6, Launch: 4300 }, Pops: 1,
};

/**
 * Nissan GT-R NISMO (R35): VR38DETT 3.8 L 60° V6, twin IHI turbos, port injection, GR6 6-speed dual-clutch rear
 * transaxle, ATTESA E-TS all-wheel drive. 600 PS at 6800 rpm, 652 Nm from 3600 to 5600 rpm, redline 7100 rpm
 * (Nissan spec sheet). Even firing every 120°, banks alternate; turbines soften the pulses; the titanium NISMO exhaust
 * merges both banks (strong cross-feed). The transaxle's gear whine and shift clunks are part of the character.
 */
export const GtrNismo = {
    Name: 'Nissan GT-R NISMO', Code: 'VR38DETT twin-turbo V6',
    Cylinders: 6, BankOffset: 120, Idle: 780, ColdIdle: 1150, Limiter: 7100, Redline: 6900,
    Header: [0.42, 0.45], Tail: [2.25, 2.32], GasSpeed: 520, CrossFeed: 0.55,
    Formants: [[105, 1.0, 3.5], [330, 1.3, 3], [820, 1.8, 2.5], [1750, 2.4, 2.5], [3300, 3, 1]],
    ValveRpm: 2900, Intake: { Box: 92, Runner: 540, Howl: 1250 },
    Spread: 0.2, Lean: 3.2, Shape: 0.25, Jitter: [0.3, 0.14], Burble: 0.6,
    Valvetrain: 0.05, Injector: 0, Space: 0.24,
    Torque: [[750, 300], [1500, 400], [2500, 560], [3300, 640], [3600, 652], [5600, 652], [6200, 640], [6800, 618], [7200, 540]],
    Motor: null,
    Turbo: { Onset: 1700, Full: 3300, Lag: 0.5, Unboosted: 0.55, Whistle: [4200, 8600], Tone: 0.02, Spool: 0.07, Vent: 0.75, Flutter: 0.4, Muffle: 2400 },
    Gearbox: { Teeth: [29, 25, 23, 21, 19, 18], Whine: 0.016, Clunk: 1, Cut: 0.06, Swap: 0.12 },
    Ratios: [4.056, 2.301, 1.595, 1.248, 1.001, 0.796], Final: 3.7, TyreRadius: 0.354, Mass: 1740 + 75, DragArea: 0.6,
    Drive: { Share: 0.95, Launch: 3900 }, Pops: 0.4, Level: 0.5,
};

/**
 * Dodge Challenger SRT Demon (2018): supercharged 6.2 L HEMI V8 with a 2.7 L twin-screw blower at 14.5 psi, 840 hp at
 * 6300 rpm and 770 lb-ft (1044 Nm) at 4500 rpm on high-octane fuel, 6500 rpm limit, TorqueFlite 8HP90 eight-speed
 * automatic with TransBrake (launch held at up to 2350 rpm), 0–60 mph 2.3 s, quarter mile 9.65 s at 140 mph (Dodge press
 * kit). Cross-plane crank, firing order 1-8-4-3-6-5-7-2: each bank fires unevenly (90/180/270° gaps), which is the V8
 * burble; long headers, little cross-feed. Stock names the blower voice (ForcedInduction), which the torque table includes.
 * Gear ratios are the 8HP90's; final drive, drag area and pipe lengths are typical values.
 */
export const Demon = {
    Name: 'Dodge Challenger SRT Demon', Code: '6.2 L supercharged HEMI V8',
    Cylinders: 8, BankOffset: 0, BankAngles: [[0, 270, 450, 540], [90, 180, 360, 630]], Idle: 760, ColdIdle: 1150, Limiter: 6500, Redline: 6300,
    Header: [0.82, 0.86], Tail: [2.7, 2.8], GasSpeed: 500, CrossFeed: 0.08,
    Formants: [[85, 1.0, 4], [240, 1.2, 3.2], [600, 1.6, 2.5], [1300, 2.2, 2], [2700, 2.8, 1]],
    ValveRpm: 2200, Intake: { Box: 80, Runner: 400, Howl: 900 },
    Spread: 0.24, Lean: 3.8, Shape: 0.27, Jitter: [0.34, 0.12], Burble: 0.95,
    Valvetrain: 0.04, Injector: 0.015, Space: 0.24,
    Torque: [[760, 430], [1500, 640], [2500, 840], [3500, 975], [4500, 1044], [5500, 1010], [6300, 949], [6600, 820]],
    Motor: null, Turbo: null, Stock: 'DemonScrew',
    Gearbox: { Teeth: [37, 33, 30, 28, 26, 24, 23, 21], Whine: 0.004, Clunk: 0.5, Cut: 0.05, Swap: 0.11 },
    Ratios: [4.71, 3.14, 2.10, 1.67, 1.29, 1.0, 0.84, 0.67], Final: 3.09, TyreRadius: 0.355, Mass: 1938 + 75, DragArea: 0.9,
    Drive: { Share: 0.62, Launch: 2350, Grip: 2.9 }, Pops: 0.5, Level: 0.55,   // Grip: drag radials + weight transfer (1.8 g launch)
};

/**
 * Porsche 918 Spyder (2013–2015), the hybrid in Porsche's 2020 Super Bowl advert "The Heist": 4.6 L naturally aspirated
 * V8 derived from the RS Spyder race engine, flat-plane crank (each bank fires evenly every 180°, the scream of two
 * racing four-cylinders), 608 hp at 8600 rpm, 530 Nm at 6600 rpm, 9150 rpm limit, top-exit exhaust straight out of the
 * engine deck (short pipes, bright and loud), 115 kW hybrid module between engine and the 7-speed PDK, 95 kW front-axle
 * motor through its own gear, decoupled above 235 km/h; 887 hp combined (Porsche press kit; PDK ratios and 3.09 final
 * drive from it). Front gear ratio, motor orders, pipe lengths and drag area are typical values.
 */
export const Spyder918 = {
    Name: 'Porsche 918 Spyder', Code: '4.6 L flat-plane V8 + 2 motors',
    Cylinders: 8, BankOffset: 90, Idle: 950, ColdIdle: 1350, Limiter: 9150, Redline: 9000,
    Header: [0.46, 0.48], Tail: [0.95, 1.0], GasSpeed: 580, CrossFeed: 0.25,
    Formants: [[150, 1.1, 2.5], [480, 1.5, 2], [1150, 2.0, 3.5], [2400, 2.6, 4], [4600, 3, 2]],
    ValveRpm: 3600, Intake: { Box: 125, Runner: 820, Howl: 1800 },
    Spread: 0.15, Lean: 2.4, Shape: 0.2, Jitter: [0.24, 0.07], Burble: 0.65,
    Valvetrain: 0.06, Injector: 0.03, Space: 0.24,
    Torque: [[900, 300], [2000, 380], [3000, 430], [4000, 470], [5000, 500], [6600, 530], [7500, 522], [8600, 496], [9300, 420]],
    Motor: { Order: 8, Carrier: 10000, Torque: 230, Front: { Torque: 210, Ratio: 8.7, Power: 95000, Cutoff: 65.3, Order: 6 } }, Turbo: null,
    Gearbox: { Teeth: [33, 29, 27, 25, 23, 22, 21], Whine: 0.005, Clunk: 0.3, Cut: 0.04, Swap: 0.065 },
    Ratios: [3.91, 2.29, 1.58, 1.19, 0.97, 0.83, 0.67], Final: 3.09, TyreRadius: 0.355, Mass: 1674 + 75, DragArea: 0.92,
    Drive: { Share: 0.95, Launch: 4200, Grip: 1.4 }, Pops: 0.7, Level: 0.6,
};

/**
 * Koenigsegg Agera RS (2015–2018, 25 built; 457 km/h peak on the 2017 Nevada record run): Koenigsegg's own 5.0 L
 * twin-turbo V8, 1160 hp at 7800 rpm on petrol, 1280 Nm at 4100 rpm (1000 Nm from 2700 to 6170), redline 8250 rpm,
 * 1.3–1.4 bar from two ball-bearing turbos, Inconel manifolds into a merge collector and one central exit, engine
 * solid-mounted in the carbon tub, 7-speed paddle shift, rear drive, 1395 kg kerb (Koenigsegg spec sheet).
 * Follow-up 22: the crank is a 90° (cross-plane) design (Koenigsegg's own engine notes; the later Jesko was the first
 * to switch to a flat plane, which "changes the exhaust note quite a bit"), so each bank fires unevenly (90/180/270°
 * gaps) and the Agera has the deep, thunderous, burbling bellow of a big V8 under its turbo whoosh, not a Ferrari-like
 * shriek. Firing order (the common cross-plane 1-8-4-3-6-5-7-2), gear ratios and pipe lengths are typical values;
 * Koenigsegg does not publish them.
 */
export const AgeraRs = {
    Name: 'Koenigsegg Agera RS', Code: '5.0 L twin-turbo cross-plane V8',
    Cylinders: 8, BankOffset: 0, BankAngles: [[0, 270, 450, 540], [90, 180, 360, 630]], Idle: 900, ColdIdle: 1300, Limiter: 8250, Redline: 8000,
    Header: [0.62, 0.66], Tail: [1.32, 1.38], GasSpeed: 530, CrossFeed: 0.35,
    Formants: [[95, 1.0, 4], [280, 1.3, 3.2], [700, 1.7, 2.8], [1550, 2.3, 2.4], [3200, 2.9, 1.2]],
    ValveRpm: 3000, Intake: { Box: 96, Runner: 560, Howl: 1300 },
    Spread: 0.22, Lean: 3.4, Shape: 0.26, Jitter: [0.32, 0.12], Burble: 0.9,
    Valvetrain: 0.045, Injector: 0.02, Space: 0.24,
    Torque: [[850, 380], [1500, 560], [2000, 800], [2700, 1000], [3400, 1180], [4100, 1280], [5000, 1240], [6170, 1180], [7000, 1110], [7800, 1060], [8300, 900]],
    Motor: null,
    Turbo: { Onset: 2200, Full: 3800, Lag: 0.55, Unboosted: 0.42, Whistle: [3500, 8800], Tone: 0.032, Spool: 0.1, Vent: 1.3, Flutter: 0.5, Muffle: 2300 },
    Gearbox: { Teeth: [31, 28, 26, 24, 23, 22, 21], Whine: 0.008, Clunk: 0.7, Cut: 0.05, Swap: 0.09 },
    Ratios: [2.92, 1.98, 1.50, 1.18, 0.95, 0.79, 0.65], Final: 3.364, TyreRadius: 0.35, Mass: 1395 + 75, DragArea: 0.7,
    Drive: { Share: 0.6, Launch: 3800, Grip: 1.6 }, Pops: 0.9, Level: 0.5,
};

/**
 * Ford Mustang GT (2024, S650): Gen 4 Coyote 5.0 L naturally aspirated cross-plane V8, port + direct injection, 486 hp at
 * 7250 rpm, 418 lb-ft (567 Nm) at 4900 rpm, 7500 rpm redline, 6-speed manual, 3.55 final drive, ~1736 kg; 0–60 mph 4.3 s,
 * quarter mile 12.6 s at 113 mph (MotorTrend; gear ratios as listed by auto123). Firing order 1-5-4-8-6-3-7-2 with cylinders
 * 1–4 on the right bank: each bank fires at 180/270/180/90° gaps, the muscle-car burble; the active exhaust's X-pipe mixes
 * the banks a little. Pipe lengths, formants and drag area are typical values; the 155 mph governor is not modelled.
 */
export const MustangGt = {
    Name: 'Ford Mustang GT', Code: '5.0 L Coyote V8',
    Cylinders: 8, BankOffset: 0, BankAngles: [[0, 180, 450, 630], [90, 270, 360, 540]], Idle: 700, ColdIdle: 1100, Limiter: 7500, Redline: 7250,
    Header: [0.76, 0.8], Tail: [2.85, 2.95], GasSpeed: 510, CrossFeed: 0.2,
    Formants: [[92, 1.0, 4], [260, 1.2, 3], [640, 1.6, 2.5], [1400, 2.2, 2], [2900, 2.8, 1]],
    ValveRpm: 2500, Intake: { Box: 84, Runner: 430, Howl: 950 },
    Spread: 0.22, Lean: 3.6, Shape: 0.26, Jitter: [0.32, 0.12], Burble: 0.9,
    Valvetrain: 0.045, Injector: 0.02, Space: 0.24,
    Torque: [[700, 330], [1500, 420], [2500, 500], [3500, 540], [4900, 567], [6000, 540], [7250, 477], [7700, 410]],
    Motor: null, Turbo: null,
    Gearbox: { Teeth: [31, 27, 24, 22, 21, 20], Whine: 0.004, Clunk: 0.6, Cut: 0.15, Swap: 0.2 },   // a quick manual shift
    Ratios: [3.23, 2.10, 1.42, 1.00, 0.81, 0.62], Final: 3.55, TyreRadius: 0.343, Mass: 1736 + 75, DragArea: 1.15,
    Drive: { Share: 0.62, Launch: 3200, Grip: 1.6, Rotating: true }, Pops: 0.45, Level: 0.55,
};

/**
 * Ford Mustang Shelby GT350 (2016–2020): 5.2 L "Voodoo" naturally aspirated V8 with a flat-plane crank (each bank fires
 * evenly every 180°, the exotic scream no other Mustang has), 526 hp at 7500 rpm, 429 lb-ft (582 Nm) at 4750 rpm, 8250
 * rpm redline, 12:1 compression, Tremec TR-3160 6-speed manual (3.25/2.23/1.61/1.24/1.00/0.63), 3.73 final drive, ~1705 kg.
 * Pipe lengths, formants and drag area are typical values.
 */
export const Gt350 = {
    Name: 'Ford Mustang Shelby GT350', Code: '5.2 L Voodoo flat-plane V8',
    Cylinders: 8, BankOffset: 90, Idle: 850, ColdIdle: 1250, Limiter: 8250, Redline: 8000,
    Header: [0.58, 0.61], Tail: [2.55, 2.62], GasSpeed: 550, CrossFeed: 0.28,
    Formants: [[128, 1.1, 3], [410, 1.5, 2.5], [1000, 2.0, 3.5], [2200, 2.6, 3.5], [4200, 3, 1.5]],
    ValveRpm: 3200, Intake: { Box: 108, Runner: 700, Howl: 1550 },
    Spread: 0.17, Lean: 2.8, Shape: 0.22, Jitter: [0.27, 0.09], Burble: 0.65,
    Valvetrain: 0.055, Injector: 0.02, Space: 0.24,
    Torque: [[850, 330], [2000, 430], [3000, 500], [4000, 560], [4750, 582], [6000, 562], [7500, 499], [8250, 440], [8500, 380]],
    Motor: null, Turbo: null,
    Gearbox: { Teeth: [32, 28, 25, 23, 21, 20], Whine: 0.005, Clunk: 0.6, Cut: 0.14, Swap: 0.19 },
    Ratios: [3.25, 2.23, 1.61, 1.24, 1.00, 0.63], Final: 3.73, TyreRadius: 0.343, Mass: 1705 + 75, DragArea: 1.0,
    Drive: { Share: 0.62, Launch: 3600, Grip: 1.6, Rotating: true }, Pops: 0.6, Level: 0.55,
};

/**
 * Ford Mustang GT3 (2024–, built with Multimatic), the Mustang Ford races in LMGT3 at Le Mans and in IMSA GTD: race 5.4 L
 * naturally aspirated cross-plane Coyote V8 developed with M-Sport, about 550 hp (set by Balance of Performance), Xtrac
 * 6-speed sequential transaxle near the rear axle with straight-cut gears and a pneumatic paddle shift, 1289 kg (Wikipedia).
 * Side exhausts exit ahead of the rear wheels, one per bank, with no silencing: the uneven per-bank firing comes through
 * raw, overrun bangs on every lift. Torque curve, ratios (for ~285 km/h), pipe lengths and limiter are typical values.
 */
export const MustangGt3 = {
    Name: 'Ford Mustang GT3', Code: '5.4 L race Coyote V8',
    Cylinders: 8, BankOffset: 0, BankAngles: [[0, 180, 450, 630], [90, 270, 360, 540]], Idle: 1100, ColdIdle: 1500, Limiter: 7800, Redline: 7600,
    Header: [0.62, 0.66], Tail: [1.15, 1.2], GasSpeed: 540, CrossFeed: 0.05,
    Formants: [[105, 1.0, 3.5], [300, 1.3, 3], [720, 1.7, 3], [1600, 2.3, 3], [3300, 3, 1.5]],
    ValveRpm: 2700, Intake: { Box: 96, Runner: 520, Howl: 1150 },
    Spread: 0.2, Lean: 3.2, Shape: 0.25, Jitter: [0.3, 0.12], Burble: 0.95,
    Valvetrain: 0.06, Injector: 0.025, Space: 0.24,
    Torque: [[1100, 400], [2000, 480], [3000, 560], [4000, 615], [5500, 650], [6500, 605], [7200, 544], [7900, 460]],
    Motor: null, Turbo: null,
    Gearbox: { Teeth: [27, 25, 23, 22, 21, 20], Whine: 0.022, Clunk: 1.2, Cut: 0.035, Swap: 0.05 },   // straight-cut sequential
    Ratios: [3.08, 2.17, 1.71, 1.41, 1.20, 1.04], Final: 3.5, TyreRadius: 0.355, Mass: 1289 + 75, DragArea: 0.95,
    Drive: { Share: 0.64, Launch: 4500, Grip: 1.8, Rotating: true }, Pops: 1.2, Level: 0.6,
};

/**
 * Ligier JS P320 (2020–2024), the Generation 2 LMP3 prototype (ELMS, Le Mans Cup, IMSA): Nissan VK56 5.6 L naturally
 * aspirated V8, ~460 hp, Xtrac 1152 6-speed sequential with straight-cut gears and Megaline paddle shift, mid-engine, rear
 * drive, traction control, 950 kg minimum, ~290 km/h top speed (Ligier / race-car listings). Cross-plane crank, firing order
 * 1-8-7-3-6-5-4-2 with odd cylinders on the right bank; short race headers into the tail of a carbon prototype. Torque
 * curve, limiter, ratios, pipe lengths and drag area are typical values.
 */
export const LigierJsP320 = {
    Name: 'Ligier JS P320 LMP3', Code: 'Nissan VK56 5.6 L V8',
    Cylinders: 8, BankOffset: 0, BankAngles: [[0, 180, 270, 450], [90, 360, 540, 630]], Idle: 1200, ColdIdle: 1600, Limiter: 7500, Redline: 7300,
    Header: [0.48, 0.5], Tail: [0.72, 0.76], GasSpeed: 560, CrossFeed: 0.32,
    Formants: [[112, 1.0, 3.5], [340, 1.3, 3], [820, 1.8, 3], [1750, 2.4, 3], [3500, 3, 1.5]],
    ValveRpm: 2800, Intake: { Box: 104, Runner: 620, Howl: 1350 },
    Spread: 0.19, Lean: 3, Shape: 0.24, Jitter: [0.29, 0.11], Burble: 0.85,
    Valvetrain: 0.06, Injector: 0.025, Space: 0.24,
    Torque: [[1200, 330], [2000, 420], [3000, 500], [4000, 545], [5000, 560], [6000, 540], [7000, 468], [7600, 400]],
    Motor: null, Turbo: null,
    Gearbox: { Teeth: [26, 24, 23, 22, 21, 20], Whine: 0.026, Clunk: 1.1, Cut: 0.03, Swap: 0.045 },   // straight-cut sequential
    Ratios: [2.69, 1.94, 1.55, 1.29, 1.11, 0.99], Final: 3.45, TyreRadius: 0.355, Mass: 950 + 75, DragArea: 0.95,
    Drive: { Share: 0.62, Launch: 5000, Grip: 1.7, Rotating: true }, Pops: 1.1, Level: 0.6,
};

export const Profiles = { LaFerrari, GtrNismo, Demon, Spyder918, AgeraRs, MustangGt, Gt350, MustangGt3, LigierJsP320 };

/** Engine torque (Nm) at full throttle (and full boost) for an engine speed, from the profile's table. */
export function FullTorque(Profile, Rpm)
{
    const Table = Profile.Torque;
    if (Rpm <= Table[0][0]) return Table[0][1] * Rpm / Table[0][0];
    for (let Index = 1; Index < Table.length; Index++)
        if (Rpm <= Table[Index][0])
        {
            const [R0, T0] = Table[Index - 1], [R1, T1] = Table[Index];
            return T0 + (T1 - T0) * (Rpm - R0) / (R1 - R0);
        }
    return Table[Table.length - 1][1];
}

/** Boost (0–1) a turbo engine settles at for a throttle and engine speed. */
export const BoostTarget = (Profile, Rpm, Throttle) => Profile.Turbo ? Math.pow(Clamp(Throttle, 0, 1), 1.5) * Smoothstep(Profile.Turbo.Onset, Profile.Turbo.Full, Rpm) : 0;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PIECES
//------------------------------------------------------------------------------------------------------------------------

/** One cylinder bank: its firing slots, live pulses, header and tail pipes, per-cylinder character and pops. */
class Bank
{
    constructor(Rate, Profile, Side, Noise)
    {
        this.Slots = Profile.Cylinders / 2;
        this.SlotAngle = 720 / this.Slots;
        this.Offset = Side * Profile.BankOffset;
        // Uneven firing (cross-plane V8): explicit slot angles per bank within the 720° cycle; else evenly spaced.
        this.Angles = Profile.BankAngles ? Profile.BankAngles[Side] : Array.from({ length: this.Slots }, (_, Slot) => Slot * this.SlotAngle);
        this.Gaps = this.Angles.map((Angle, Slot) => ((Angle - this.Angles[(Slot + this.Slots - 1) % this.Slots]) % 720 + 720) % 720 || 720);
        this.Slot = -1;
        this.Pulse = { Amp: 0, Shift: 0, Attack: 10, Decay: 30, Rough: 0 };
        this.Tail = { Amp: 0, Shift: 0, Attack: 10, Decay: 30, Rough: 0 };
        this.Pop = { Amp: 0, At: 999, Crackle: 0, Age: 0 };
        this.Header = new PipeSection(Rate, Profile.Header[Side], { Speed: Profile.GasSpeed, Reflect: -0.5, Loss: 5200 });
        this.Pipe = new PipeSection(Rate, Profile.Tail[Side], { Speed: Profile.GasSpeed * 0.82, Reflect: -0.38, Loss: 2600 });
        this.Turbine = new OnePole(Rate, 6000);
        // Each cylinder: strength, timing lean (degrees) and pulse shape from its own runner length.
        this.Spread = Array.from({ length: this.Slots }, () => Math.max(0.4, 1 + Noise.Gauss() * Profile.Spread));
        this.Leans = Array.from({ length: this.Slots }, () => Noise.Gauss() * Profile.Lean);
        this.Shapes = Array.from({ length: this.Slots }, () => Math.max(0.55, 1 + Noise.Gauss() * Profile.Shape));
        this.Crack = 0;
    }
}

/** Pressure of one blowdown pulse at Phase degrees after its slot (raised-cosine rise, exponential fall, combustion noise). */
function PulseAt(Pulse, Phase, Noise)
{
    const Angle = Phase - Pulse.Shift;
    if (Angle <= 0 || Pulse.Amp === 0) return 0;
    let Shape;
    if (Angle < Pulse.Attack) Shape = 0.5 - 0.5 * Math.cos(Math.PI * Angle / Pulse.Attack);
    else
    {
        const Fall = (Angle - Pulse.Attack) / Pulse.Decay;
        if (Fall > 7) return 0;
        Shape = Math.exp(-Fall) - 0.22 * Math.exp(-Fall * 0.35) * (1 - Math.exp(-Fall * 3));   // the scavenging dip
    }
    return Pulse.Amp * (Shape + Pulse.Rough * Noise.White() * Math.exp(-Angle / (Pulse.Attack * 2.5)));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENGINE
//------------------------------------------------------------------------------------------------------------------------

/**
 * Live engine voice. Set the drive inputs with Feed(), then Render() adds stereo samples into the given buffers.
 * Feed: { Rpm, Throttle 0–1, Load 0–1 (delivered torque share), Ignition 0–1 (0 = cut), Motor −1–1 (electric torque,
 * negative = regeneration), Starter 0–1, Boost 0–1 (turbo; estimated here when absent), Spool 0–1 (turbo shaft speed from
 * the integrator, so the whistle carries the real lag), Gear (for whine), Clunk (a
 * counter that steps on every gear change) }. Pickup: 'Chase' (microphone behind the car), 'Cabin' (driver seat) or
 * 'Dry' (point source for the trackside fly-by, both channels equal, no reflections).
 */
export class PulseTrainEngine
{
    constructor(Rate, Profile = LaFerrari, { Seed = 11, Pickup = 'Chase' } = {})
    {
        this.Rate = Rate;
        this.Profile = Profile;
        this.Pickup = Pickup;
        this.Noise = new SeededNoise(Seed);
        this.Crank = 0;
        this.Rpm = new Slew(Rate, 0.012, 0);
        this.Throttle = new Slew(Rate, 0.02, 0);
        this.Held = new Slew(Rate, 0.15, 0);                          // slow throttle follower, detects a sharp lift
        this.Load = new Slew(Rate, 0.03, 0);
        this.Ignition = 1;
        this.Motor = new Slew(Rate, 0.05, 0);
        this.Starter = new Slew(Rate, 0.02, 0);
        this.Valve = new Slew(Rate, 0.18, 0);
        this.Overrun = 0;                                              // unburnt-fuel energy left after a lift, drives pops
        this.Banks = [0, 1].map((Side) => new Bank(Rate, Profile, Side, this.Noise));
        this.Blocks = [new DcBlock(Rate, 18), new DcBlock(Rate, 18)];
        this.Gear = 0; this.Clunk = 0; this.ClunkHit = 0; this.ExternalBoost = null; this.ExternalSpool = null;

        // Muffler: two paths blended by the bypass valve, then the body formants.
        this.Quiet = [0, 1].map(() => [new Biquad(Rate).Shape('Low', 1100, 0.6), new Biquad(Rate).Shape('Peak', 95, 1.2, 5)]);
        this.Loud = [0, 1].map(() => [new Biquad(Rate).Shape('Low', 8200, 0.6)]);
        this.Formants = [0, 1].map(() => Profile.Formants.map(([Hz, Q, Gain]) => new Biquad(Rate).Shape('Peak', Hz, Q, Gain)));

        // Intake: airbox Helmholtz, runner resonance and the high howl, plus turbulent hiss.
        const { Box, Runner, Howl } = Profile.Intake;
        this.Airbox = new Biquad(Rate).Shape('Band', Box, 2.5);
        this.Runner = new Biquad(Rate).Shape('Band', Runner, 5);
        this.Howl = new Biquad(Rate).Shape('Band', Howl, 6);
        this.Hiss = [new Biquad(Rate).Shape('High', 900, 0.7), new Biquad(Rate).Shape('Low', 5200, 0.7)];

        // Mechanical: valve closing ticks, injector ticks, high shimmer, whines, shift clunk.
        this.ValveSlot = -1; this.ValveHit = 0; this.InjectorHit = 0;
        this.ValveRing = new ModeBank(Rate, [[2300, 0.005], [3900, 0.0035, 0.7], [6100, 0.0025, 0.45]]);
        this.InjectorRing = new ModeBank(Rate, [[7600, 0.002], [11200, 0.0014, 0.6]]);
        this.ClunkRing = new ModeBank(Rate, [[165, 0.07], [540, 0.045, 0.7], [1350, 0.03, 0.5], [2900, 0.014, 0.3]]);
        this.Shimmer = [new Biquad(Rate).Shape('High', 4200, 0.7), new Biquad(Rate).Shape('Low', 9500, 0.7)];
        this.WhinePhase = 0; this.GearPhase = 0; this.MotorPhase = 0; this.CarrierPhase = 0; this.StarterPhase = 0;

        // Turbos: shaft speed, whistle per turbo, spool whoosh, blow-off vent and compressor surge flutter.
        if (Profile.Turbo)
        {
            this.Boost = new Slew(Rate, Profile.Turbo.Lag, 0);
            this.Shaft = new Slew(Rate, 0.25, 0);
            // Follow-up 20: the same aero-acoustic compressor, blow-off valve and surge as the add-on inducers
            // (ForcedInduction.js), twin wheels slightly detuned, instead of two sines and plain filtered noise.
            this.Wheel = new Compressor(Rate, { Twins: 2, Detune: 1.013, Seed: 141, Spool: [900, 3400] });
            this.Dump = new BlowOff(Rate, 'Atmos', 151); this.Stall = new Surge(Rate, 161);
            this.GateBand = new Biquad(Rate).Shape('Band', 3200, 0.9);
        }

        // Pickup colouring and space.
        this.CabinExhaust = [0, 1].map(() => [new Biquad(Rate).Shape('Low', 1100, 0.7), new Biquad(Rate).Shape('LowShelf', 140, 0.8, 5)]);
        this.CabinIntake = new Biquad(Rate).Shape('Peak', 420, 0.9, 4);
        this.Air = [new OnePole(Rate, 15000), new OnePole(Rate, 15000)];
        this.Echo = new DelayLine(Math.ceil(Rate * 0.04));
        this.EchoTone = [new OnePole(Rate, 4800), new OnePole(Rate, 4800)];
        this.Taps = [[0.0071, 0.0173, 0.0299], [0.0097, 0.0211, 0.0337]].map((Side) => Side.map((Seconds) => Seconds * Rate));
    }

    Feed({ Rpm = 0, Throttle = 0, Load = Throttle, Ignition = 1, Motor = 0, Starter = 0, Boost, Spool, Gear, Clunk, FrontRpm = 0 })
    {
        this.Rpm.Target = Math.max(0, Rpm);
        this.Throttle.Target = Clamp(Throttle, 0, 1);
        this.Load.Target = Clamp(Load, 0, 1);
        this.Ignition = Clamp(Ignition, 0, 1);
        this.Motor.Target = Clamp(Motor, -1, 1);
        this.Starter.Target = Clamp(Starter, 0, 1);
        this.ExternalBoost = Boost ?? null;
        this.ExternalSpool = Spool ?? null;
        this.FrontRpm = FrontRpm;
        if (Gear !== undefined) this.Gear = Gear;
        if (Clunk !== undefined && Clunk !== this.Clunk) { this.ClunkHit = 1; this.Clunk = Clunk; }
    }

    /** Starts a new firing in a bank: amplitude, timing scatter, edge sharpness, and maybe an afterfire pop. */
    Fire(Bank, Slot, Rpm, Throttle, Load, Shaft)
    {
        const P = this.Profile, Noise = this.Noise;
        Object.assign(Bank.Tail, Bank.Pulse);
        const Spin = Clamp(Rpm / P.Redline, 0, 1.1);
        const Cranking = Rpm < 500;
        const Lifted = Throttle < 0.03 && Rpm > P.Idle + 450;
        let Strength = Cranking ? 0.3 : Lifted ? 0.085 : 0.13 + 0.87 * Math.pow(Load, 0.85);
        const Burnt = this.Ignition;
        if (!Cranking && Noise.Next() > Burnt) Strength *= 0.06;     // ignition cut: the charge passes unburnt
        const Pulse = Bank.Pulse;
        const Jitter = Mix(P.Jitter[0], P.Jitter[1], Smoothstep(0, 0.8, Spin)) * (Load < 0.15 ? 1.25 : 1);
        // Tuned headers even the cylinders out near the top of the range: lumpy low down, a clean scream up high.
        const Evening = Smoothstep(0.3, 1, Spin), Spread = 1 + (Bank.Spread[Slot] - 1) * Mix(1, 0.4, Evening), Shape = 1 + (Bank.Shapes[Slot] - 1) * Mix(1, 0.5, Evening);
        let Amp = Strength * Spread * Math.max(0.05, 1 + Noise.Gauss() * Jitter) * (0.45 + 0.55 * Spin + 0.2 * Load);
        if (Lifted) Amp *= Mix(1, Noise.Range(0.1, 1.9), P.Burble);  // lean overrun: lumpy, uneven burble
        Pulse.Amp = Amp * (P.Turbo ? 1 - 0.3 * Shaft : 1);
        Pulse.Shift = Bank.Leans[Slot] * Mix(1, 0.5, Evening) + Noise.Gauss() * Mix(3.2, 0.7, Spin);
        const AttackMs = Mix(0.95, 0.24, Load) * (Lifted ? 1.4 : 1) * Shape;
        Pulse.Attack = Clamp(AttackMs * Rpm * 0.006, 2.5, 60);
        Pulse.Decay = Mix(0.2, 0.33, Load) * Bank.SlotAngle * Shape;
        Pulse.Rough = Mix(0.12, 0.32, Load * Spin) + (Lifted ? 0.2 : 0);
        this.InjectorHit = Cranking ? 0 : Noise.Range(0.6, 1);

        // Afterfire: unburnt fuel lighting in the hot exhaust on overrun or during an ignition cut.
        const Cut = Burnt < 0.5 && !Cranking;
        const Chance = (Lifted ? 0.0045 * this.Overrun * Smoothstep(2600, 7200, Rpm) : 0) + (Cut ? 0.045 * Smoothstep(3500, 8000, Rpm) : 0);
        if (Noise.Next() < Chance * P.Pops)
        {
            Bank.Pop.Amp = Noise.Range(1.6, 3.6) * (Cut ? 1 : 0.85);
            Bank.Pop.At = Noise.Range(8, 95);
            Bank.Pop.Crackle = Noise.Range(0.6, 1.4);
            Bank.Pop.Age = 0;
        }
    }

    /** Adds Count stereo samples starting at Offset. */
    Render(Left, Right, Offset = 0, Count = Left.length - Offset)
    {
        const P = this.Profile, Noise = this.Noise, Rate = this.Rate, Turbo = P.Turbo;
        const [Bank0, Bank1] = this.Banks;
        const Pickup = this.Pickup;
        const [PanL0, PanR0] = PanGains(Pickup === 'Dry' ? 0 : -0.38), [PanL1, PanR1] = PanGains(Pickup === 'Dry' ? 0 : 0.38);
        const FiringAngle = 720 / P.Cylinders, ValveAngle = 720 / (P.Cylinders * 2), ShimmerAngle = 720 / (P.Cylinders * 4);
        for (let Index = Offset; Index < Offset + Count; Index++)
        {
            const Rpm = this.Rpm.Run(), Throttle = this.Throttle.Run(), Load = this.Load.Run(), Motor = this.Motor.Run(), Starter = this.Starter.Run();
            const Held = this.Held.Run(); this.Held.Target = Throttle;
            const Spin = Rpm / P.Redline;
            const Step = Rpm * 6 / Rate;                                // crank degrees per sample
            this.Crank = (this.Crank + Step) % 720;
            if (Throttle > 0.5 && Rpm > 3000) this.Overrun = 1; else this.Overrun *= 1 - 1 / (1.4 * Rate);
            this.Valve.Target = (Rpm > P.ValveRpm && Load > 0.25) || Rpm > P.Redline * 0.58 ? 1 : 0;
            const Valve = this.Valve.Run();

            // Turbo: boost (fed or estimated), shaft speed, wastegate, lift detection.
            let Shaft = 0, Gate = 0;
            if (Turbo)
            {
                this.Boost.Target = this.ExternalBoost ?? BoostTarget(P, Rpm, Throttle);
                const Boost = this.ExternalBoost ?? this.Boost.Run();
                if (this.ExternalSpool !== null) Shaft = this.Shaft.Current += (Clamp(this.ExternalSpool, 0, 1.1) - this.Shaft.Current) * 0.002;   // the integrator's shaft (real lag)
                else { this.Shaft.Target = Math.sqrt(Clamp(Boost, 0, 1)) * 0.85 + 0.15 * Smoothstep(800, 6000, Rpm); Shaft = this.Shaft.Run(); }
                Gate = Smoothstep(0.85, 1, Boost) * Smoothstep(4500, 6500, Rpm);
                if (Held - Throttle > 0.35 && Boost > 0.3 && !this.Dump.Busy) { this.Dump.Fire(Boost); if (Turbo.Flutter) this.Stall.Fire(Boost); }
                const Cutoff = Mix(Mix(5500, Turbo.Muffle, Shaft), 7500, Gate * 0.5);
                Bank0.Turbine.Set(Cutoff); Bank1.Turbine.Set(Cutoff);
            }

            // Exhaust: two banks.
            let Out0 = 0, Out1 = 0;
            for (let Side = 0; Side < 2; Side++)
            {
                const Bank = this.Banks[Side];
                const Local = ((this.Crank - Bank.Offset) % 720 + 720) % 720;
                let Slot = Bank.Slots - 1;
                while (Slot > 0 && Local < Bank.Angles[Slot]) Slot--;
                if (Local < Bank.Angles[0]) Slot = Bank.Slots - 1;
                const Phase = ((Local - Bank.Angles[Slot]) % 720 + 720) % 720;
                if (Slot !== Bank.Slot) { if (Rpm > 30) this.Fire(Bank, Slot, Rpm, Throttle, Load, Shaft); Bank.Slot = Slot; }
                let Excite = PulseAt(Bank.Pulse, Phase, Noise) + PulseAt(Bank.Tail, Phase + Bank.Gaps[Slot], Noise);
                if (Turbo) Excite = Bank.Turbine.Run(Excite) * 1.15;
                let Crack = 0;
                const Pop = Bank.Pop;
                if (Pop.Amp > 0)
                {
                    Pop.Age += Step;
                    const Since = (Pop.Age - Pop.At) / Math.max(Step, 1e-6) / Rate * 1000;     // ms since the pop lit
                    if (Since > 0)
                    {
                        Excite += Pop.Amp * (Since < 0.09 ? Since / 0.09 : Math.exp(-(Since - 0.09) / 0.7));
                        Crack = Pop.Crackle * Noise.White() * Math.exp(-Since / 3.5) * (Noise.Next() < 0.35 ? 2.2 : 0.6);
                        if (Since > 12) Pop.Amp = 0;
                    }
                }
                const Out = Bank.Header.Run(Excite);
                if (Side === 0) Out0 = Out; else Out1 = Out;
                Bank.Crack = Crack;
            }
            const Feed = P.CrossFeed;
            const Pipe0 = Bank0.Pipe.Run(Out0 + Out1 * Feed + Bank0.Crack);
            const Pipe1 = Bank1.Pipe.Run(Out1 + Out0 * Feed + Bank1.Crack);
            const Exhaust = [Pipe0, Pipe1].map((Signal, Side) =>
            {
                let Quiet = Signal;
                for (const Filter of this.Quiet[Side]) Quiet = Filter.Run(Quiet);
                let Loud = Signal;
                for (const Filter of this.Loud[Side]) Loud = Filter.Run(Loud);
                let Mixed = Quiet * (1 - Valve) * 0.75 + Loud * Valve * 1.1;
                for (const Filter of this.Formants[Side]) Mixed = Filter.Run(Mixed);
                return this.Blocks[Side].Run(Mixed) * (P.Level ?? 1);
            });

            // Intake: suction pulses after each firing, through airbox, runners and howl, plus airflow hiss.
            const Airflow = Math.pow(Clamp(Throttle, 0.04, 1), 0.7) * Math.pow(Clamp(Spin, 0, 1.05), 1.3) * (Turbo ? 0.7 + 0.5 * Shaft : 1);
            const IntakePhase = ((this.Crank + 250) % FiringAngle) / FiringAngle;
            const Suction = Math.pow(Math.sin(Math.PI * IntakePhase), 6) * (0.4 + Throttle) * (Turbo ? 1 - 0.5 * Shaft : 1);
            const Turbulence = Noise.Pink() * (0.35 + Suction) * Airflow;
            const Hiss = this.Hiss[1].Run(this.Hiss[0].Run(Turbulence));
            const Intake = (this.Airbox.Run(Suction) * 1.6 + this.Runner.Run(Suction) * 1.2 + this.Howl.Run(Suction + Turbulence * 0.6) * (0.5 + 1.5 * Airflow)) * (0.25 + Airflow) + Hiss * 0.5;

            // Mechanical chatter: a tick per valve closing (random strength), injector ticks, high shimmer, whines.
            const ValveSlot = Math.floor(this.Crank / ValveAngle);
            if (ValveSlot !== this.ValveSlot) { this.ValveSlot = ValveSlot; this.ValveHit = Rpm > 200 ? Noise.Range(0.35, 1) : 0; }
            const Valves = this.ValveRing.Run(this.ValveHit) * P.Valvetrain * (0.55 + 0.8 * Spin);
            const Injectors = this.InjectorRing.Run(this.InjectorHit) * P.Injector * (1 - 0.6 * Clamp(Spin, 0, 1));
            this.ValveHit = 0; this.InjectorHit = 0;
            const Valvetrain = Math.exp(-((this.Crank % ShimmerAngle) / ShimmerAngle) * 6) * Noise.White();
            const Shimmer = this.Shimmer[1].Run(this.Shimmer[0].Run(Valvetrain)) * (0.012 + 0.04 * Spin * Spin);
            this.WhinePhase = (this.WhinePhase + Rpm / 60 * 24 / Rate) % 1;
            const Whine = Math.sin(Tau * this.WhinePhase) * 0.006 * Spin * Spin;
            let Gearing = 0;
            if (this.Gear > 0 && P.Gearbox)
            {
                this.GearPhase = (this.GearPhase + Rpm / 60 * P.Gearbox.Teeth[this.Gear - 1] / Rate) % 1;
                Gearing = (Math.sin(Tau * this.GearPhase) + 0.3 * Math.sin(Tau * this.GearPhase * 2)) * P.Gearbox.Whine * (0.3 + 0.7 * Math.max(Load, Throttle < 0.05 ? 0.6 : 0)) * Smoothstep(1200, 5000, Rpm);
            }
            let Clunk = 0;
            if (P.Gearbox)
            {
                const Hit = this.ClunkHit > 0 ? Noise.White() * this.ClunkHit : 0;
                this.ClunkHit = Math.max(0, this.ClunkHit - 1 / (0.003 * Rate));
                Clunk = this.ClunkRing.Run(Hit * 0.25) * P.Gearbox.Clunk;
            }
            const Mechanical = Valves + Injectors + Shimmer + Whine + Gearing + Clunk;

            // Hybrid motor: electrical order tone and harmonics, inverter carrier with side-bands; louder under torque.
            let Electric = 0;
            if (P.Motor)
            {
                const Drive = Math.abs(Motor);
                this.MotorPhase = (this.MotorPhase + Rpm / 60 * P.Motor.Order / Rate) % 1;
                this.CarrierPhase = (this.CarrierPhase + P.Motor.Carrier / Rate) % 1;
                Electric = Drive * (Math.sin(Tau * this.MotorPhase) * 0.05 + Math.sin(Tau * this.MotorPhase * 3) * 0.02
                    + Math.sin(Tau * this.CarrierPhase) * Math.sin(Tau * this.MotorPhase) * 0.012) * (Motor < 0 ? 0.7 : 1);
                // Front-axle motor (918): its own whine locked to wheel speed through its gear, silent once decoupled.
                if (P.Motor.Front && this.FrontRpm > 1)
                {
                    this.FrontPhase = ((this.FrontPhase ?? 0) + this.FrontRpm / 60 * P.Motor.Front.Order / Rate) % 1;
                    Electric += Drive * Math.sin(Tau * this.FrontPhase) * 0.035 * Math.min(1, (Rate * 0.4) / Math.max(1, this.FrontRpm / 60 * P.Motor.Front.Order));
                }
            }

            // Turbo sounds: twin whistles (slightly detuned, beating), spool whoosh, blow-off vent, surge flutter, wastegate.
            let Forced = 0;
            if (Turbo)
            {
                const Pitch = Mix(Turbo.Whistle[0], Turbo.Whistle[1], Shaft);
                const [WhistleMid, , RushMid] = this.Wheel.Run(Pitch, Shaft, Throttle, Turbo.Spool);
                const Whistle = WhistleMid * Turbo.Tone * 1.6 * Shaft * Shaft * (0.35 + 0.65 * Throttle);
                const Vent = this.Dump.Run()[0] * Turbo.Vent * 1.4;
                const Surge = Turbo.Flutter ? this.Stall.Run() * Turbo.Flutter * 1.6 : 0;
                const Wastegate = this.GateBand.Run(Noise.White()) * Gate * 0.05;
                Forced = Whistle + RushMid + Vent + Surge + Wastegate;
            }

            // Starter: pinion whine with brush noise; each compression stroke loads it (speed and level dip).
            let Crank = 0;
            if (Starter > 0.001)
            {
                const Compression = 0.5 - 0.5 * Math.cos(this.Crank * (P.Cylinders / 2) * Math.PI / 180);
                this.StarterPhase = (this.StarterPhase + (1350 + Rpm * 1.3) * (1 - 0.18 * Compression) / Rate) % 1;
                Crank = Starter * (0.55 + 0.45 * (1 - Compression)) * (Math.sin(Tau * this.StarterPhase) * 0.05 + Math.sin(Tau * this.StarterPhase * 2.01) * 0.025 + Noise.White() * 0.02);
            }

            // Pickup mix.
            let Left0, Right0;
            if (Pickup === 'Cabin')
            {
                let E0 = Exhaust[0], E1 = Exhaust[1];
                for (const Filter of this.CabinExhaust[0]) E0 = Filter.Run(E0);
                for (const Filter of this.CabinExhaust[1]) E1 = Filter.Run(E1);
                const Induction = this.CabinIntake.Run(Intake);
                const Centre = Induction * 0.9 + Mechanical * 0.55 + Electric * 0.6 + Crank * 0.7 + Forced * 0.9;
                Left0 = E0 * 0.36 + E1 * 0.21 + Centre;
                Right0 = E1 * 0.36 + E0 * 0.21 + Centre;
            }
            else
            {
                const Centre = Intake * 0.22 + Mechanical * 0.3 + Electric * 0.45 + Crank * 0.5 + Forced * 0.55;
                Left0 = Exhaust[0] * PanL0 + Exhaust[1] * PanL1 + Centre;
                Right0 = Exhaust[0] * PanR0 + Exhaust[1] * PanR1 + Centre;
                if (Pickup === 'Chase' && P.Space)
                {
                    // Early reflections off the road and nearby surfaces: depth instead of a dry, flat point source.
                    this.Echo.Write((Left0 + Right0) * 0.5);
                    const [TapsL, TapsR] = this.Taps;
                    const ReflectL = this.Echo.Read(TapsL[0]) * 0.34 + this.Echo.Read(TapsL[1]) * 0.22 + this.Echo.Read(TapsL[2]) * 0.14;
                    const ReflectR = this.Echo.Read(TapsR[0]) * 0.34 + this.Echo.Read(TapsR[1]) * 0.22 + this.Echo.Read(TapsR[2]) * 0.14;
                    Left0 += this.EchoTone[0].Run(ReflectL) * P.Space * 2;
                    Right0 += this.EchoTone[1].Run(ReflectR) * P.Space * 2;
                }
            }
            Left[Index] += SoftClip(this.Air[0].Run(Left0) * 0.9, 0.7);
            Right[Index] += SoftClip(this.Air[1].Run(Right0) * 0.9, 0.7);
        }
    }
}

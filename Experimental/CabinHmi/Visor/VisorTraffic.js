// VISOR TRAFFIC
// 🧩 Other cars on the night pass: oncoming traffic in the far lane, and slower cars ahead in your own lane that you
//    catch up with and overtake. Each one is a recoloured copy of the ControlVehicle with headlights (glows plus a light
//    pool on the snow), tail and brake lamps, and wheels that turn. Drivers keep a time gap to whoever is ahead (another
//    car or you) and stop for the porcupine and the traffic signal.
// 📝 Lanes: +1 travels +X in z = +LaneOffset (your lane); −1 travels −X in z = −LaneOffset (oncoming).

import { THREE, Family } from '../Shared/VehicleAssembly.js';
import { LaneOffset, GlowSprite } from './VisorCourse.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

export const CarLength = 6.22;                                    // [m]
export const CarWidth  = 2.5;                                     // [m]
const Paints = ['#8c1d22', '#d6dadf', '#1d3a68', '#2b2c31', '#b58a2c', '#3b5a3d', '#5a2a6e', '#6d7681'];
const Kmh = 3.6;
const OncomingCount = 5;
const AheadCount    = 2;

function Rand(Low, High) { return Low + Math.random() * (High - Low); }

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRAFFIC
//------------------------------------------------------------------------------------------------------------------------

/**
 * @param {THREE.Scene} Scene
 * @param {{ Root: THREE.Group, Finish: object }} Template - the assembled ControlVehicle to copy
 * @param {(Car: object) => number} StopGap - metres from a car's front bumper to the next thing it must stop for
 */
export function MakeTraffic(Scene, Template, StopGap)
{
    const PoolMap = (() =>
    {
        const Canvas = document.createElement('canvas');
        Canvas.width = Canvas.height = 128;
        const Context = Canvas.getContext('2d');
        const Fill = Context.createRadialGradient(64, 64, 0, 64, 64, 64);
        Fill.addColorStop(0, 'rgba(255,255,255,0.9)');
        Fill.addColorStop(0.5, 'rgba(255,255,255,0.3)');
        Fill.addColorStop(1, 'rgba(255,255,255,0)');
        Context.fillStyle = Fill;
        Context.fillRect(0, 0, 128, 128);
        return new THREE.CanvasTexture(Canvas);
    })();
    const ClearGlass = Template.Finish[Family.Glass].clone();
    ClearGlass.visible = true;
    let Serial = 0;

    function Build()
    {
        const Group = Template.Root.clone(true);
        const Paint = Template.Finish[Family.BodyCoat].clone();
        Paint.color.set(Paints[Serial % Paints.length]);
        const Tail = Template.Finish[Family.Taillight].clone();
        const Lens = Template.Finish[Family.Headlight].clone();         // dimmer lens: the glow sprites carry the light
        Lens.emissiveIntensity = 0.45; Lens.color.set('#8d97ab');
        Group.traverse((Part) =>
        {
            if (!Part.isMesh || !Array.isArray(Part.material)) return;
            Part.material = Part.material.map((Material) => Material === Template.Finish[Family.BodyCoat] ? Paint : Material === Template.Finish[Family.Glass] ? ClearGlass : Material === Template.Finish[Family.Taillight] ? Tail : Material === Template.Finish[Family.Headlight] ? Lens : Material);
        });
        const Spins = Group.children.filter((Part) => Part.children[0] && Part !== Group.children[0]).map((Steer) => Steer.children[0]);
        const Heads = [-0.82, 0.82].map((Side) => { const Glow = GlowSprite('#eef4ff', 2.6); Glow.position.set(3.15, 0.95, Side); Group.add(Glow); return Glow; });
        const Tails = [-0.85, 0.85].map((Side) => { const Glow = GlowSprite('#ff2418', 1.2); Glow.position.set(-3.15, 1.08, Side); Group.add(Glow); return Glow; });
        const Beam = new THREE.Mesh(new THREE.PlaneGeometry(26, 9).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ map: PoolMap, color: '#c9d6ff', transparent: true, opacity: 0.32, blending: THREE.AdditiveBlending, depthWrite: false, fog: true }));
        Beam.position.set(16, 0.03, 0);
        Beam.userData.Unpickable = true;                                  // pings pass through the light pool
        Group.add(Beam);
        Scene.add(Group);
        return { Id: ++Serial, Group, Spins, Heads, Tails, Tail, Beam, Live: false, Lane: 1, X: 0, Z: 0, Speed: 0, Want: 0, Braking: false, Behind: false };
    }
    const Cars = Array.from({ length: OncomingCount + AheadCount }, Build);
    for (const Car of Cars) Car.Group.visible = false;

    function Spawn(Car, Lane, X, Speed)
    {
        Object.assign(Car, { Live: true, Lane, X, Z: Lane * LaneOffset, Speed, Want: Speed, Braking: false, Behind: false, LastForward: undefined });
        Car.Group.visible = true;
        Car.Group.rotation.y = Lane > 0 ? 0 : Math.PI;
        Car.Group.position.set(X, 0, Car.Z);
    }
    const Live = (Lane) => Cars.filter((Car) => Car.Live && Car.Lane === Lane);
    const Clear = (Lane, X, Spacing) => Live(Lane).every((Car) => Math.abs(Car.X - X) > Spacing);

    // Keeps the road populated around the player: oncoming cars appear far ahead, slow cars ahead in your lane.
    function Populate(Player, First = false)
    {
        for (const Car of Cars)
        {
            if (!Car.Live) continue;
            const Along = Car.X - Player.X;
            if ((Car.Lane < 0 && (Along < -60 || Along > 2600)) || (Car.Lane > 0 && (Along < -40 || Along > 1100))) { Car.Live = false; Car.Group.visible = false; }
        }
        const Idle = Cars.filter((Car) => !Car.Live);
        // Oncoming cars come in platoons (short gaps) with now and then a long empty stretch: the overtaking windows.
        while (Live(-1).length < OncomingCount && Idle.length)
        {
            const Farthest = Live(-1).reduce((Far, Car) => Math.max(Far, Car.X), Player.X + (First ? 60 : 380));
            const X = Farthest + (Math.random() < 0.3 ? Rand(600, 1100) : Rand(80, 170));
            Spawn(Idle.pop(), -1, X, Rand(68, 92) / Kmh);
        }
        while (Live(1).length < AheadCount && Idle.length)
        {
            const X = Player.X + (First ? (Live(1).length ? Rand(480, 700) : Rand(170, 230)) : Rand(380, 650));
            if (!Clear(1, X, 140)) break;
            Spawn(Idle.pop(), 1, X, Rand(50, 66) / Kmh);
        }
    }

    // Car following: keep 1.6 s plus 7 m to whatever is ahead in the lane, and stop short of stop points.
    function Drive(Step, Player)
    {
        for (const Car of Cars)
        {
            if (!Car.Live) continue;
            let Gap = Infinity, LeadSpeed = Car.Want;
            for (const Other of Cars)
            {
                if (Other === Car || !Other.Live || Other.Lane !== Car.Lane) continue;
                const Ahead = (Other.X - Car.X) * Car.Lane - CarLength;
                if (Ahead > -1 && Ahead < Gap) { Gap = Ahead; LeadSpeed = Other.Speed; }
            }
            if (Math.abs(Player.Z - Car.Z) < 2.6)
            {
                const Ahead = (Player.X - Car.X) * Car.Lane - CarLength;
                const Toward = Math.cos(Player.Heading) * Car.Lane;            // +1 same way, −1 head-on
                if (Ahead > -1 && Ahead < Gap) { Gap = Ahead; LeadSpeed = Player.Speed * Toward; }
            }
            let Goal = Car.Want;
            if (Gap < 200) Goal = Math.min(Goal, Math.max(0, LeadSpeed + (Gap - 7 - Car.Speed * 1.6) * 0.45));
            const Stop = StopGap(Car);
            if (Stop < 160) Goal = Math.min(Goal, Math.sqrt(2 * 3.2 * Math.max(0, Stop)));
            const Rate = Goal > Car.Speed ? Math.min(1.8, (Goal - Car.Speed) * 0.8) : Math.max(-7.5, (Goal - Car.Speed) * 1.8);
            Car.Braking = Rate < -0.6 || (Car.Speed < 0.3 && Goal < 0.3);
            Car.Speed = Math.max(0, Car.Speed + Rate * Step);
            Car.X += Car.Speed * Car.Lane * Step;
        }
    }

    // Visuals once per frame: position, wheel spin, lamps that only shine toward the viewer they face.
    const ToEye = new THREE.Vector3();
    function Show(Eye)
    {
        for (const Car of Cars)
        {
            if (!Car.Live) continue;
            Car.Group.position.set(Car.X, 0, Car.Z);
            for (const Spin of Car.Spins) Spin.rotation.z -= Car.Speed * 0.016 / 0.509;
            ToEye.set(Eye.x - Car.X, 0, Eye.z - Car.Z).normalize();
            const Facing = ToEye.x * Car.Lane;                                    // +1 when the car faces the eye
            for (const Glow of Car.Heads) Glow.material.opacity = THREE.MathUtils.clamp(Facing * 1.4, 0, 1);
            for (const Glow of Car.Tails) Glow.material.opacity = THREE.MathUtils.clamp(-Facing * 1.4, 0, 1) * (Car.Braking ? 1 : 0.45);
            for (const Glow of Car.Tails) Glow.scale.setScalar(Car.Braking ? 1.9 : 1.2);
            Car.Tail.emissiveIntensity = Car.Braking ? 5 : 1.6;
        }
    }

    return {
        Cars,
        get Live() { return Cars.filter((Car) => Car.Live); },
        Start(Player) { Populate(Player, true); },
        Update(Step, Player) { Drive(Step, Player); },
        Present(Player, Eye) { Populate(Player); Show(Eye); },
        Remove(Car) { Car.Live = false; Car.Group.visible = false; },
        // Clears the road and (optionally) refills it around the player: used after a jump down the course.
        Reset(Player, Fill = true) { for (const Car of Cars) { Car.Live = false; Car.Group.visible = false; } if (Fill) Populate(Player, true); },
        // Puts one car at X in a lane (reusing a free car, or the farthest one) for tests and captures.
        Place(Lane, X, Speed)
        {
            const Car = Cars.find((Each) => !Each.Live) ?? Cars.reduce((Far, Each) => (Math.abs(Each.X - X) > Math.abs(Far.X - X) ? Each : Far));
            Spawn(Car, Lane, X, Speed);
            return Car;
        },
    };
}

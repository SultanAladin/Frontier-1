// CABIN LINK
// 🧩 Couples a dashboard cluster and the centre display of the same car when both are open in one browser
//    (two tabs, two windows, or the cockpit view). The cluster owns the driving: it sends speed, gear, pedals and
//    drive mode. The centre display owns the lamp switches: it sends the lamp state. Either side runs alone.
// 📝 BroadcastChannel only, no server. Channel per car: "CabinLink.Contour", "CabinLink.Afterglow", ...

/**
 * @param {string} Car
 * @returns {{ Send: (Kind: string, Payload: object) => void, Fresh: (Kind: string, Within?: number) => object|null, On: (Listener: Function) => void }}
 */
export function OpenLink(Car)
{
    const Channel   = 'BroadcastChannel' in window ? new BroadcastChannel(`CabinLink.${Car}`) : null;
    const Heard     = {};                                               // Kind → { Payload, At }
    const Listeners = [];
    if (Channel) Channel.onmessage = (Message) =>
    {
        const { Kind, Payload } = Message.data || {};
        if (!Kind) return;
        Heard[Kind] = { Payload, At: performance.now() };
        Listeners.forEach((Listener) => Listener(Kind, Payload));
    };
    return {
        Send(Kind, Payload) { if (Channel) Channel.postMessage({ Kind, Payload }); },
        Fresh(Kind, Within = 1500) { const Last = Heard[Kind]; return Last && performance.now() - Last.At < Within ? Last.Payload : null; },
        On(Listener) { Listeners.push(Listener); },
    };
}

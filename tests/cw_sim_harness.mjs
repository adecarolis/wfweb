// Shared pieces of the CW node sims (cw_keyer_sim.mjs, cw_paddle_sim.mjs):
// a virtual clock with timers, a fake AudioContext that records the
// sidetone's envelope automation, and a recording rig.
// ---- virtual clock + timers -------------------------------------------------
export class Clock {
  constructor() { this.t = 1_000_000; this.timers = []; this.id = 0; }
  now() { return this.t; }
  setTimeout(fn, ms) { const id = ++this.id; this.timers.push({ id, at: this.t + Math.max(0, ms), fn }); return id; }
  clearTimeout(id) { this.timers = this.timers.filter(x => x.id !== id); }
  // advance to time T, firing timers in order
  run(until) {
    for (;;) {
      this.timers.sort((a, b) => a.at - b.at || a.id - b.id);
      if (!this.timers.length || this.timers[0].at > until) break;
      const x = this.timers.shift(); this.t = x.at; x.fn();
    }
    this.t = until;
  }
  advance(ms) { this.run(this.t + ms); }
}

// ---- fake AudioContext: records envelope automation ------------------------
export function fakeContext(clock) {
  const ctx = { state: 'running', destination: {}, resume() { this.state = 'running'; }, close() { this.state = 'closed'; },
    baseLatency: 0.006, outputLatency: 0.168 };
  Object.defineProperty(ctx, 'currentTime', { get: () => (clock.now() - 1_000_000) / 1000 + 10 });
  const param = (events) => ({
    value: 0,
    setValueAtTime(v, t) { events.push({ k: 'set', v, t }); },
    linearRampToValueAtTime(v, t) { events.push({ k: 'ramp', v, t }); },
    setTargetAtTime(v, t) { events.push({ k: 'target', v, t }); },
    cancelAndHoldAtTime(t) { events.push({ k: 'cancel', t }); },
    cancelScheduledValues(t) { events.push({ k: 'cancel', t }); },
  });
  ctx.envEvents = [];
  ctx.createGain = () => { const ev = []; const g = { gain: param(ev), connect() {}, disconnect() {}, _ev: ev }; ctx.lastGain = g; return g; };
  ctx.oscillators = [];
  ctx.createOscillator = () => { const o = { type: '', frequency: param([]), started: false, stopped: false, connect() {}, disconnect() {}, start() { this.started = true; }, stop() { this.stopped = true; } }; ctx.oscillators.push(o); return o; };
  return ctx;
}
// Tone bursts from envelope events: a 'set 0' followed by 'ramp 1' opens, the
// next 'ramp 0' closes; a cancel at time T drops everything after T.
export function bursts(events) {
  const out = []; let cur = null;
  for (const e of events) {
    if (e.k === 'cancel') {   // everything scheduled after T is gone, a running burst is cut at T
      while (out.length && out[out.length - 1].on > e.t) out.pop();
      if (out.length && out[out.length - 1].off > e.t) out[out.length - 1].off = e.t;
      if (cur) { if (cur.on > e.t) cur = null; else { cur.off = e.t; out.push(cur); cur = null; } }
      continue;
    }
    if (e.k === 'ramp' && e.v === 1 && cur === null) cur = { on: e.t - 0.005 };
    else if (e.k === 'ramp' && e.v === 0 && cur !== null) { cur.off = e.t; out.push(cur); cur = null; }
  }
  return out;
}
// wall-clock ms of a fake audio time (see fakeContext)
export const wall = (t) => (t - 10) * 1000 + 1_000_000;

export function makeRig(clock) {
  const rig = { frames: [], stops: 0 };
  rig.send = (text) => rig.frames.push({ t: clock.now(), text });
  rig.stop = () => { rig.stops++; rig.frames.push({ t: clock.now(), text: null }); };
  return rig;
}

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

// ---- the rig's keyer on the air ---------------------------------------------
// What the rig keys for the frames it was handed ([{t, text}], text null =
// cut), modelled on the virtual rig (tools/virtualrig synthesizeCw /
// drainCwTx, onCwSendRequested), as measured off the IC-7300
// (tests/cw_rig_capture.mjs --paddle / --chr, 2026-11-28): a frame that
// arrives while it is keying is queued behind a three-dot character gap
// (seven for a space); one that arrives once its last element has ended
// waits out the rest of the character gap, except within half a dot of
// that end, where it starts at once and runs into the letter before.
// readAir reads the elements back by their gaps at the rig's dot, as the
// other station's ear does: under two dots the same character, under five
// the next character, else a new word.
export function rigOnAir(frames, dot, CW_MORSE) {
  const els = []; let free = -Infinity, gapDue = false;
  for (const f of frames) {
    if (f.text === null) { free = -Infinity; gapDue = false; continue; }
    let t = f.t;
    if (t < free) t = free;
    else if (gapDue) {                                  // ran dry after a letter
      if (t - free >= 0.5 * dot && t - free < 3 * dot) t = free + 3 * dot;
      gapDue = false;
    }
    for (const ch of f.text.replace(/\^([A-Z]{2})/g, '<$1>').match(/<[A-Z]{2}>|./g)) {
      if (ch === ' ') { t += (gapDue ? 3 : 0) * dot + 4 * dot; gapDue = false; continue; }
      if (gapDue) t += 3 * dot;
      const p = CW_MORSE[ch];
      for (let k = 0; k < p.length; k++) {
        if (k) t += dot;
        const len = (p[k] === '.' ? 1 : 3) * dot;
        els.push({ k: p[k], on: t, off: t + len }); t += len;
      }
      gapDue = true;
    }
    free = t;
  }
  return els;
}
export function readAir(els, dot, cwFromPattern) {
  let out = '', pat = '';
  for (let i = 0; i < els.length; i++) {
    if (i) {
      const g = (els[i].on - els[i - 1].off) / dot;
      if (g >= 2) { out += cwFromPattern(pat) || '*'; pat = ''; if (g >= 5) out += ' '; }
    }
    pat += els[i].k;
  }
  return out + (pat ? cwFromPattern(pat) || '*' : '');
}

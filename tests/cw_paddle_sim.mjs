// Drives resources/web-shared/cw-paddle.js (paddle / straight key as CW text
// entry) under node with a virtual clock, a fake AudioContext and a real
// WfCwKeyer with a recording rig.  Synthetic paddle-contact timelines (what a
// MIDI controller such as the CTR2-MIDI sends: note on = closed, note off =
// open) must decode to the text they spell, in iambic mode A and B, swapped
// or not, at several speeds; a jittery straight key must decode too, from a
// dot estimate set for another speed.  The browser sidetone must follow the
// paddle, and each decoded character must reach the rig through the text
// path without being toned a second time.  Exit code 1 on a failed check.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { Clock, fakeContext, bursts, wall, makeRig } from './cw_sim_harness.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const W = {};
for (const f of ['cw-keyer.js', 'cw-paddle.js']) new Function('window', fs.readFileSync(path.join(here, '../resources/web-shared', f), 'utf8'))(W);
const { CW_MORSE, WfCwKeyer, WfCwSidetone, WfCwPaddle, cwFromPattern } = W;

let failures = 0;
function check(ok, msg) { console.log((ok ? 'PASS ' : 'FAIL ') + msg); if (!ok) failures++; }
const close = (a, b, tol) => Math.abs(a - b) <= tol;

const timers = (clock) => ({ now: () => clock.now(), setTimeout: (f, ms) => clock.setTimeout(f, ms), clearTimeout: (t) => clock.clearTimeout(t) });

// A page in miniature: sidetone, text keyer, paddle feeding the keyer.
function makeStation(clock, opts = {}) {
  const wpm = opts.wpm || 20;
  const rig = makeRig(clock);
  const tone = new WfCwSidetone({ enabled: true, createContext: () => fakeContext(clock), pitch: () => 600, gain: () => 0.2, ...timers(clock) });
  const keyer = new WfCwKeyer({ wpm: () => wpm, send: rig.send, stopRig: rig.stop, sidetone: tone, ...timers(clock) });
  const st = { rig, tone, keyer, text: '', elements: [], unknown: [], mute: [] };
  st.paddle = new WfCwPaddle({
    dot: () => keyer.dot(), sidetone: tone, mode: opts.mode, swap: opts.swap, ...timers(clock),
    onChar: (ch) => { st.text += ch; keyer.sendText(ch, { tone: false }); },
    onUnknown: (p, ch) => st.unknown.push([p, ch]),
    onRxMute: (m) => st.mute.push({ t: clock.now(), m }),
    rigEnd: () => keyer.characters.length ? keyer.base + keyer.nextTime : 0,
    rxDelay: () => 400,
  });
  const onEl = st.paddle.keyer._onElement;
  st.paddle.keyer._onElement = (k, on, off) => { st.elements.push({ k, on, off }); onEl(k, on, off); };
  return st;
}

// Paddle contacts that spell text on an iambic keyer at the given dot: the
// first element of a character is pressed when the keyer is idle, the next
// ones half-way through the gap before them (so the keyer picks them at the
// end of the gap), each released half-way through its element.
function paddleTimeline(text, dot, t0, swap = false) {
  const ev = [];
  const side = (el) => ((el === '.') !== swap) ? 'left' : 'right';
  let prevOff = null, gap = 0;
  for (const ch of text) {
    if (ch === ' ') { gap = 7; continue; }
    const p = CW_MORSE[ch];
    for (let k = 0; k < p.length; k++) {
      let press, start;
      if (k === 0) { start = prevOff === null ? t0 : prevOff + (gap || 3) * dot; press = start; }
      else { press = prevOff + 0.5 * dot; start = prevOff + dot; }
      const len = (p[k] === '.' ? 1 : 3) * dot;
      ev.push({ t: press, side: side(p[k]), closed: true });
      ev.push({ t: start + 0.5 * len, side: side(p[k]), closed: false });
      prevOff = start + len;
    }
    gap = 0;
  }
  return { ev, end: prevOff };
}
function play(clock, ev, fn) {
  for (const e of ev.slice().sort((a, b) => a.t - b.t)) { clock.run(e.t); fn(e); }
}

// ---- 1. iambic: text -> contacts -> text, modes A and B, three speeds -----
for (const mode of ['B', 'A']) {
  for (const wpm of [15, 25, 35]) {
    const clock = new Clock(), st = makeStation(clock, { wpm, mode });
    const text = 'CQ TEST DE W1AW';
    const tl = paddleTimeline(text, st.keyer.dot(), clock.now() + 100);
    play(clock, tl.ev, (e) => st.paddle.paddle(e.side, e.closed));
    clock.run(tl.end + 10 * st.keyer.dot());
    check(st.text === text + ' ', `mode ${mode} at ${wpm} WPM: paddle decodes "${text}" (got "${st.text}")`);
  }
}

// ---- 2. every character the keyer knows round-trips ------------------------
{
  const clock = new Clock(), st = makeStation(clock, { wpm: 25 });
  const chars = Object.keys(CW_MORSE).filter(c => c.length === 1 && c !== ' ' && cwFromPattern(CW_MORSE[c]) === c);
  const text = chars.join(' ');
  const tl = paddleTimeline(text, st.keyer.dot(), clock.now() + 100);
  play(clock, tl.ev, (e) => st.paddle.paddle(e.side, e.closed));
  clock.run(tl.end + 10 * st.keyer.dot());
  check(st.text === text + ' ', `all ${chars.length} keyer characters decode (got "${st.text}")`);
  check(cwFromPattern('.-.-.') === '+' && cwFromPattern('-...-') === '=' && cwFromPattern('...-.-') === '<SK>', 'shared patterns decode to the plain character, prosign-only ones to the prosign');
}

// ---- 3. swap ----------------------------------------------------------------
{
  const clock = new Clock(), st = makeStation(clock, { wpm: 20, swap: true });
  const tl = paddleTimeline('KN', st.keyer.dot(), clock.now() + 100, true);
  play(clock, tl.ev, (e) => st.paddle.paddle(e.side, e.closed));
  clock.run(tl.end + 10 * st.keyer.dot());
  check(st.text === 'KN ', 'swapped paddles: right is the dit (got "' + st.text + '")');
  for (const [swap, want] of [[false, 'E '], [true, 'T ']]) {
    const c2 = new Clock(), s2 = makeStation(c2, { wpm: 20, swap });
    s2.paddle.paddle('left', true); c2.advance(20); s2.paddle.paddle('left', false);
    c2.advance(2000);
    check(s2.text === want, `a tap on the left paddle, ${swap ? 'swapped' : 'unswapped'}: "${want.trim()}" (got "${s2.text}")`);
  }
}

// ---- 4. squeeze: mode A stops, mode B adds the alternate element ------------
for (const [mode, want] of [['A', 'K'], ['B', 'C']]) {
  const clock = new Clock(), st = makeStation(clock, { wpm: 20, mode });
  const d = st.keyer.dot(), t0 = clock.now() + 100;
  clock.run(t0); st.paddle.paddle('right', true);          // dah first
  clock.run(t0 + 0.5 * d); st.paddle.paddle('left', true);  // squeeze during it
  clock.run(t0 + 7 * d);                                    // inside the third element (dah, 6d..9d)
  st.paddle.paddle('left', false); st.paddle.paddle('right', false);
  clock.run(t0 + 30 * d);
  check(st.elements.map(e => e.k).join('') === (mode === 'A' ? '-.-' : '-.-.') && st.text === want + ' ',
    `mode ${mode}: squeeze released during the third element keys ${want} (got ${st.elements.map(e => e.k).join('')} = "${st.text}")`);
  const gaps = st.elements.slice(1).map((e, i) => e.on - st.elements[i].off);
  check(gaps.every(g => close(g, d, 1e-6)), `mode ${mode}: squeezed elements one dot apart`);
}

// ---- 5. mode B memory: a dah tapped during a dit is not lost; mode A drops it
for (const [mode, want] of [['A', 'E'], ['B', 'A']]) {
  const clock = new Clock(), st = makeStation(clock, { wpm: 20, mode });
  const d = st.keyer.dot(), t0 = clock.now() + 100;
  clock.run(t0); st.paddle.paddle('left', true);
  clock.run(t0 + 0.3 * d); st.paddle.paddle('right', true);
  clock.run(t0 + 0.6 * d); st.paddle.paddle('right', false); st.paddle.paddle('left', false);
  clock.run(t0 + 30 * d);
  check(st.text === want + ' ', `mode ${mode}: a dah tapped and released during a dit -> ${want} (got "${st.text}")`);
}

// ---- 6. the sidetone follows the paddle, the text path adds no tone --------
{
  const clock = new Clock(), st = makeStation(clock, { wpm: 25 });
  const tl = paddleTimeline('PARIS PARIS', st.keyer.dot(), clock.now() + 100);
  play(clock, tl.ev, (e) => st.paddle.paddle(e.side, e.closed));
  clock.run(tl.end + 3000);
  const b = bursts(st.tone.env ? st.tone.env._ev : st.tone.ctx.lastGain._ev);
  const all = st.tone.ctx.oscillators.length;
  check(b.length === st.elements.length, `one tone burst per paddle element (${b.length} of ${st.elements.length}), none from the text path`);
  let err = 0;
  for (let i = 0; i < Math.min(b.length, st.elements.length); i++) err = Math.max(err, Math.abs(wall(b[i].on) - st.elements[i].on), Math.abs(wall(b[i].off) - st.elements[i].off));
  check(err < 1, `tone bursts sit on the paddle elements (max error ${err.toFixed(2)} ms)`);
  check(st.keyer.elements(0).length === 0 && st.keyer.characters.every(c => c.noTone), 'paddle text is flagged noTone in the keyer model');
  check(all >= 1, 'oscillator started by the paddle');
}

// ---- 7. decoded characters reach the rig, one character behind -------------
{
  const clock = new Clock(), st = makeStation(clock, { wpm: 20 });
  const d = st.keyer.dot();
  const tl = paddleTimeline('CQ CQ DE W1AW K', d, clock.now() + 100);
  play(clock, tl.ev, (e) => st.paddle.paddle(e.side, e.closed));
  clock.run(tl.end + 20000);
  const sent = st.rig.frames.filter(f => f.text !== null);
  check(sent.map(f => f.text).join('') === 'CQ CQ DE W1AW K ', 'the rig gets the decoded text (' + JSON.stringify(sent.map(f => f.text).join('')) + ')');
  const cEnd = st.elements[3].off;   // C = -.-. : fourth element
  check(close(sent[0].t, cEnd + 2 * d, 1), `first character handed to the rig two dots after its last element (${(sent[0].t - cEnd).toFixed(1)} ms)`);
  check(st.rig.stops === 0, 'the rig keyer is never cut');
}

// ---- 8. a pattern that is no character: its longest known head, then -----
// one extra element dropped, two or more decoded as further characters
for (const [pattern, want] of [['......', '5'], ['..-.-', 'F'], ['..-.--', 'FM'], ['--.....', '7I'], ['.-.-.-.-', '.A'], ['.-...', '<AS>']]) {
  const clock = new Clock(), st = makeStation(clock, { wpm: 20 });
  const d = st.keyer.dot(), t0 = clock.now() + 100;
  // one paddle contact per element, pressed in the gap before it (as paddleTimeline)
  let prevOff = null;
  for (let k = 0; k < pattern.length; k++) {
    const start = k === 0 ? t0 : prevOff + d, press = k === 0 ? start : prevOff + 0.5 * d;
    const len = (pattern[k] === '.' ? 1 : 3) * d, side = pattern[k] === '.' ? 'left' : 'right';
    clock.run(press); st.paddle.paddle(side, true);
    clock.run(start + 0.5 * len); st.paddle.paddle(side, false);
    prevOff = start + len;
  }
  clock.run(prevOff + 40 * d);
  const sent = st.rig.frames.filter(f => f.text !== null).map(f => f.text).join('');
  const known = !st.unknown.length;
  check(st.text === want + ' ' && sent === want.replace(/<([A-Z]{2})>/, '^$1') + ' ',
    `${pattern} -> ${want}${known ? ' (a known prosign)' : ''}, keyed as ${JSON.stringify(sent)}`);
  if (!known) check(st.unknown[0][0] === pattern && st.unknown[0][1] === want, `${pattern}: reported with what was sent instead`);
}

// ---- 8b. K1FM with F and M run together: -.- .---- ..-.-- ----------------
{
  const clock = new Clock(), st = makeStation(clock, { wpm: 20 });
  const d = st.keyer.dot();
  let prevOff = null, t = clock.now() + 100;
  for (const pattern of ['-.-', '.----', '..-.--']) {
    for (let k = 0; k < pattern.length; k++) {
      const start = k === 0 ? (prevOff === null ? t : prevOff + 3 * d) : prevOff + d, press = k === 0 ? start : prevOff + 0.5 * d;
      const len = (pattern[k] === '.' ? 1 : 3) * d, side = pattern[k] === '.' ? 'left' : 'right';
      clock.run(press); st.paddle.paddle(side, true);
      clock.run(start + 0.5 * len); st.paddle.paddle(side, false);
      prevOff = start + len;
    }
  }
  clock.run(prevOff + 40 * d);
  check(st.text === 'K1FM ' && st.rig.frames.filter(f => f.text !== null).map(f => f.text).join('') === 'K1FM ', '-.- .---- ..-.-- sends K1FM (got ' + JSON.stringify(st.text) + ')');
}

// ---- 9. straight key: adaptive decode with jitter --------------------------
function rng(seed) { let s = seed >>> 0; return () => ((s = (s * 1664525 + 1013904223) >>> 0) / 2 ** 32); }
function keyTimeline(text, dot, t0, jitter, seed) {
  const r = rng(seed), j = () => 1 + (r() * 2 - 1) * jitter;
  const ev = []; let t = t0, first = true;
  for (const ch of text) {
    if (ch === ' ') { t += 4 * dot * j(); continue; }   // 3 + 4 = 7
    if (!first) t += 3 * dot * j();
    const p = CW_MORSE[ch];
    for (let k = 0; k < p.length; k++) {
      if (k) t += dot * j();
      const len = (p[k] === '.' ? 1 : 3) * dot * j();
      ev.push({ t, closed: true }); ev.push({ t: t + len, closed: false });
      t += len;
    }
    first = false;
  }
  return { ev, end: t };
}
for (const opWpm of [12, 25]) {
  for (const seed of [1, 2, 3]) {
    const clock = new Clock(), st = makeStation(clock, { wpm: 20 });
    const text = 'CQ TEST DE W1AW 73';
    const tl = keyTimeline(text, 1200 / opWpm, clock.now() + 100, 0.15, seed);
    play(clock, tl.ev, (e) => st.paddle.straightKey(e.closed));
    clock.run(tl.end + 3000);
    check(st.text === text + ' ', `straight key at ${opWpm} WPM (set 20), ±15 % jitter, seed ${seed}: "${st.text}"`);
    if (seed === 1) check(close(st.paddle.straight.dot(), 1200 / opWpm, 0.25 * 1200 / opWpm), `dot estimate follows the operator: ${st.paddle.straight.dot().toFixed(1)} ms for ${(1200 / opWpm).toFixed(1)}`);
  }
}

// ---- 10. straight key: the tone follows the key, bounce ignored -------------
{
  const clock = new Clock(), st = makeStation(clock, { wpm: 20 });
  const t0 = clock.now() + 100;
  const tl = keyTimeline('TEST', 60, t0, 0, 1);
  play(clock, tl.ev, (e) => st.paddle.straightKey(e.closed));
  clock.run(tl.end + 50);
  const b = bursts(st.tone.env._ev);
  const downs = tl.ev.filter(e => e.closed), ups = tl.ev.filter(e => !e.closed);
  let err = 0;
  for (let i = 0; i < b.length; i++) err = Math.max(err, Math.abs(wall(b[i].on) - downs[i].t), Math.abs(wall(b[i].off) - ups[i].t));
  check(b.length === downs.length && err < 1, `straight-key tone: one burst per key-down, on the key (${b.length}/${downs.length}, max error ${err.toFixed(2)} ms)`);
  clock.run(tl.end + 2000);
  check(st.tone.osc === null, 'oscillator parked after the key stays up');
  st.text = '';
  const t1 = clock.now();
  st.paddle.straightKey(true); clock.run(t1 + 5); st.paddle.straightKey(false);   // 5 ms blip
  clock.run(t1 + 2000);
  check(st.text === '', 'a 5 ms contact bounce decodes as nothing');
}

// ---- 11. stop drops the half-keyed character --------------------------------
{
  const clock = new Clock(), st = makeStation(clock, { wpm: 20 });
  const t0 = clock.now() + 100;
  clock.run(t0); st.paddle.paddle('right', true);
  clock.run(t0 + 100); st.paddle.stop();
  clock.run(t0 + 3000);
  check(st.text === '' && st.rig.frames.length === 0, 'stop() mid-character: nothing decoded, nothing sent');
}

// ---- 12. RX audio cut on the first element, back after the radio's echo ----
{
  const clock = new Clock(), st = makeStation(clock, { wpm: 20 });
  const d = st.keyer.dot();
  const tl = paddleTimeline('CQ CQ', d, clock.now() + 100);
  play(clock, tl.ev, (e) => st.paddle.paddle(e.side, e.closed));
  clock.run(tl.end + 20000);
  const rigEnd = st.keyer.base + st.keyer.nextTime;
  check(st.mute.length === 2 && st.mute[0].m === true && st.mute[1].m === false, 'one mute, one unmute for a paddled "CQ CQ" (' + JSON.stringify(st.mute.map(x => x.m)) + ')');
  check(st.mute[0].t === st.elements[0].on, 'muted on the first element, at once');
  check(st.mute[1].t >= rigEnd + 400 && st.mute[1].t <= rigEnd + 400 + 1, `unmuted when the radio's keying plus the RX delay is over (${(st.mute[1].t - rigEnd).toFixed(0)} ms after the radio's end)`);
  check(st.text === 'CQ CQ ' && st.rig.frames.map(f => f.text).join('') === 'CQ CQ ', 'the text still reaches the rig');
}
{
  // two overs with a pause longer than the window: two mute cycles
  const clock = new Clock(), st = makeStation(clock, { wpm: 25 });
  const d = st.keyer.dot();
  let tl = paddleTimeline('K', d, clock.now() + 100);
  play(clock, tl.ev, (e) => st.paddle.paddle(e.side, e.closed));
  clock.run(tl.end + 5000);
  tl = paddleTimeline('R', d, clock.now() + 100);
  play(clock, tl.ev, (e) => st.paddle.paddle(e.side, e.closed));
  clock.run(tl.end + 5000);
  check(st.mute.map(x => x.m).join() === 'true,false,true,false', 'two overs, two mute windows');
}
{
  // straight key held 2 s: muted throughout, released after
  const clock = new Clock(), st = makeStation(clock, { wpm: 20 });
  const t0 = clock.now() + 100;
  clock.run(t0); st.paddle.straightKey(true);
  clock.run(t0 + 2000);
  check(st.mute.length === 1 && st.mute[0].m === true && st.mute[0].t === t0, 'straight key: muted on key-down and for as long as it is held');
  st.paddle.straightKey(false);
  clock.run(t0 + 6000);
  check(st.mute.length === 2 && st.mute[1].m === false, 'straight key: unmuted after release');
  const c2 = new Clock(), s2 = makeStation(c2, { wpm: 20 });
  const t1 = c2.now() + 100;
  c2.run(t1); s2.paddle.straightKey(true); c2.run(t1 + 4); s2.paddle.straightKey(false);   // bounce
  c2.run(t1 + 3000);
  check(s2.mute.map(x => x.m).join() === 'true,false', 'a contact bounce does not leave the RX muted');
}

console.log(failures ? 'FAILED ' + failures : 'ALL PASS');
process.exit(failures ? 1 : 0);

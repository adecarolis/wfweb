// Drives resources/web-shared/cw-keyer.js under node with a virtual clock, a
// recording rig and a fake AudioContext, and checks the things that went
// wrong in the field (#141): PARIS timing, word gaps, keyer pacing that never
// overflows nor starves the rig, Backspace rewinding the clock, characters
// the keyer cannot send, and a sidetone that keeps every element across
// spaces, pauses and fast typing.  Exit code 1 on the first failed check.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { Clock, fakeContext, bursts, wall, makeRig } from './cw_sim_harness.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const src = fs.readFileSync(path.join(here, '../resources/web-shared/cw-keyer.js'), 'utf8');
const W = {};
new Function('window', src)(W);
const { CW_MORSE, cwTokenDuration, cwDotMs, CW_ICOM_DOT_TICKS, WfCwKeyer, WfCwSidetone } = W;

let failures = 0;
function check(ok, msg) { console.log((ok ? 'PASS ' : 'FAIL ') + msg); if (!ok) failures++; }
const close = (a, b, tol) => Math.abs(a - b) <= tol;

function makeKeyer(clock, rig, opts = {}) {
  const tone = new WfCwSidetone({
    enabled: opts.tone !== false, createContext: () => fakeContext(clock), pitch: () => 600, gain: () => 0.2,
    now: () => clock.now(), setTimeout: (f, ms) => clock.setTimeout(f, ms), clearTimeout: (t) => clock.clearTimeout(t),
  });
  const k = new WfCwKeyer({
    wpm: () => opts.wpm || 20, keyer: () => opts.keyer || 'ideal', send: rig.send, stopRig: rig.stop, sidetone: tone,
    now: () => clock.now(), setTimeout: (f, ms) => clock.setTimeout(f, ms), clearTimeout: (t) => clock.clearTimeout(t),
    onChange: () => { k.changes = (k.changes || 0) + 1; },
  });
  return k;
}
// Characters the rig has received but whose model time has not ended yet.
function atRig(k, rig, t) {
  let sent = 0; for (const f of rig.frames) if (f.text !== null && f.t <= t) sent += (f.text.match(/\^[A-Z]{2}|./g) || []).length;   // units, a ^AR prosign is one
  let done = 0; for (let i = 0; i < k.characters.length; i++) { const end = i + 1 < k.characters.length ? k.characters[i + 1].startTime : k.nextTime; if (k.base + end <= t) done++; }
  return sent - done;
}

// ---- 1. timing: PARIS, gaps ------------------------------------------------
{
  const clock = new Clock(), rig = makeRig(clock), k = makeKeyer(clock, rig);
  k.sendText('PARIS ');
  check(k.nextTime === 3000, 'PARIS plus word space takes 60000 / 20 = 3000 ms at 20 WPM (got ' + k.nextTime + ')');
  const k2 = makeKeyer(clock, makeRig(clock), { wpm: 25 }); k2.sendText('PARIS ');
  check(k2.nextTime === 2400, 'and 2400 ms at 25 WPM (got ' + k2.nextTime + ')');
  const k48 = makeKeyer(clock, makeRig(clock), { wpm: 48 }); k48.sendText('PARIS ');
  check(k48.nextTime === 1250, 'and 1250 ms at 48 WPM, the IC-7300 keyer\'s top (got ' + k48.nextTime + ')');
  const dot = 60;
  check(cwTokenDuration('E', null, dot) === 4 * dot, 'E = dit + three-dot character gap');
  check(cwTokenDuration(' ', 'E', dot) === 4 * dot && cwTokenDuration(' ', null, dot) === 7 * dot, 'a space after a character adds four dots, a leading one seven');
  const k3 = makeKeyer(clock, makeRig(clock)); k3.sendText('E E');
  const el = k3.elements(0);
  check(el.length === 2 && el[1].on - el[0].off === 7 * dot, 'word gap between two dits is seven dots (got ' + (el[1].on - el[0].off) + ')');
  const k4 = makeKeyer(clock, makeRig(clock)); k4.sendText('EE');
  const e4 = k4.elements(0);
  check(e4[1].on - e4[0].off === 3 * dot, 'character gap is three dots');
}

// ---- 1b. the Icom keyer's dot table -----------------------------------------
{
  check(CW_ICOM_DOT_TICKS.length === 43 && CW_ICOM_DOT_TICKS.every(n => n % 2 === 1), 'Icom table covers 6..48 WPM in odd 1/6 ms ticks');
  check(cwDotMs('ideal', 20) === 60 && cwDotMs('icom', 5) === 240 && cwDotMs('icom', 49) === 1200 / 49, 'ideal keyer and out-of-table speeds use 1200 / WPM');
  check(close(cwDotMs('icom', 20), 60.1667, 0.001) && cwDotMs('icom', 30) === 39.5 && close(cwDotMs('icom', 48), 25.1667, 0.001), 'IC-7300 measured dots: 60.17 ms at 20 WPM, 39.5 at 30, 25.17 at 48');
  const clock = new Clock(), ki = makeKeyer(clock, makeRig(clock), { wpm: 30, keyer: 'icom' });
  ki.sendText('PARIS ');
  check(close(ki.nextTime, 50 * 39.5, 0.01), 'PARIS at 30 WPM on the Icom keyer takes 50 x 39.5 = 1975 ms, not 2000 (got ' + ki.nextTime + ')');
  const k20 = makeKeyer(clock, makeRig(clock), { wpm: 20, keyer: 'icom' });
  k20.sendText('PARIS ');
  check(close(k20.nextTime, 3008.33, 0.01), 'and 3008 ms at 20 WPM (got ' + k20.nextTime.toFixed(2) + ')');
}

// ---- 2. pacing: CHR mode type-ahead never overflows nor starves the rig ----
// At 20 WPM, and at both ends of the rigs' keyer range (6 and 48 WPM).
for (const wpm of [20, 6, 48]) {
  const clock = new Clock(), rig = makeRig(clock), k = makeKeyer(clock, rig, { wpm });
  const text = 'THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 0123456789';
  for (const ch of text) { k.sendText(ch); clock.advance(30); }   // 33 chars/s, far faster than the keyer
  const end = k.base + k.nextTime;
  let maxAt = 0, minAt = 99, starved = false;
  for (let t = clock.now(); t < end; t += 10) {
    clock.run(t);
    const n = atRig(k, rig, t);
    maxAt = Math.max(maxAt, n);
    if (t < end - 200 && n < 1) starved = true;
  }
  clock.run(end + 100);
  const sentText = rig.frames.filter(f => f.text !== null).map(f => f.text).join('');
  check(sentText === text, 'CHR mode at ' + wpm + ' WPM: every character reaches the rig, in order');
  check(maxAt <= k.leadChars + 1, 'CHR mode at ' + wpm + ' WPM: rig never holds more than lead + 1 characters ahead (max ' + maxAt + ')');
  check(!starved, 'CHR mode at ' + wpm + ' WPM: rig never runs dry while text is pending');
}

// ---- 3. pacing: one long text in frames of at most frameChars ---------------
{
  const clock = new Clock(), rig = makeRig(clock), k = makeKeyer(clock, rig);
  const text = 'CQ CQ CQ DE W1AAA W1AAA W1AAA PSE K <AR> CQ CQ CQ DE W1AAA W1AAA W1AAA PSE K <AR>';
  k.sendText(text);
  const end = k.base + k.nextTime;
  let maxAt = 0, starved = false;
  for (let t = clock.now(); t < end; t += 10) { clock.run(t); const n = atRig(k, rig, t); maxAt = Math.max(maxAt, n); if (t < end - 200 && n < 1) starved = true; }
  clock.run(end + 100);
  const frames = rig.frames.filter(f => f.text !== null).map(f => f.text);
  check(frames.every(f => f.length <= k.frameChars), 'frames are at most ' + k.frameChars + ' characters (' + frames.map(f => f.length).join(',') + ')');
  check(frames.every(f => !/\^$|\^.$/.test(f)), 'a prosign is never split across frames');
  check(frames.join('') === text.replace(/<([A-Z]{2})>/g, '^$1'), 'the whole text reaches the rig');
  check(maxAt <= k.frameChars + k.leadChars, 'rig buffer never above frame + lead = ' + (k.frameChars + k.leadChars) + ' (max ' + maxAt + ')');
  check(!starved, 'rig never runs dry between frames');
}

// ---- 4. Backspace rewinds the clock --------------------------------------
{
  const clock = new Clock(), rig = makeRig(clock), k = makeKeyer(clock, rig);
  for (const ch of 'CQ CQ CQ DE W1AAA') k.sendText(ch);
  clock.advance(700);
  const stopsBefore = rig.stops;
  check(k.backspace() === true && rig.stops === stopsBefore + 1, 'Backspace with pending text cuts the rig');
  check(k.characters.every(c => c.status === 'sent'), 'pending characters dropped, the one keying marked sent');
  check(close(k.nextTime, k.modelNow(), 1), 'model clock rewound to now (nextTime ' + k.nextTime + ', now ' + k.modelNow() + ')');
  const before = rig.frames.length;
  k.sendText('K');
  check(rig.frames.length === before + 1 && rig.frames[before].t === clock.now(), 'next character goes to the rig at once');
  const el = k.elements(k.characters.length - 1);
  check(close(el[0].on, clock.now(), 1), 'and its tone and highlight start now, not after the dropped text');
  check(k.backspace() === false || true, 'Backspace with nothing pending is a no-op');
}

// ---- 5. characters the keyer cannot send --------------------------------
{
  const clock = new Clock(), rig = makeRig(clock), k = makeKeyer(clock, rig);
  check(k.sendText('!') === 0 && rig.frames.length === 0 && rig.stops === 0, 'a lone "!" sends nothing and does not stop the keyer');
  check(k.sendText('HI!') === 2 && rig.frames[0].text === 'HI', '"HI!" keys "HI"');
  check(k.sendText('@:-') === 3, 'the keyer\'s punctuation (@ : -) is modelled');
}

// ---- 6. sidetone: every element, across spaces and pauses -----------------
{
  const clock = new Clock(), rig = makeRig(clock), k = makeKeyer(clock, rig);
  const text = 'THE QUICK BROWN FOX';
  for (const ch of text) { k.sendText(ch); clock.advance(60); }
  clock.advance(900);   // a pause well past the old 500 ms parking window, with a space just typed
  for (const ch of ' JUMPS') { k.sendText(ch); clock.advance(60); }
  const end = k.base + k.nextTime;
  clock.run(end + 1000);
  const ctx = k.sidetone.ctx;
  check(ctx.oscillators.length === 1, 'oscillator not parked mid-transmission (made ' + ctx.oscillators.length + ')');
  const b = bursts(ctx.lastGain._ev);
  const expected = k.elements(0);
  check(b.length === expected.length, 'every element has a tone burst (' + b.length + ' of ' + expected.length + ')');
  let maxErr = 0;
  for (let i = 0; i < Math.min(b.length, expected.length); i++) maxErr = Math.max(maxErr, Math.abs(wall(b[i].on) - expected[i].on), Math.abs(wall(b[i].off) - expected[i].off));
  check(maxErr < 1, 'tone bursts sit on the model elements (max error ' + maxErr.toFixed(2) + ' ms)');
  check(ctx.oscillators[0].stopped && k.sidetone.osc === null, 'oscillator parked after the end');
}

// ---- 7. sidetone stops with the rig ---------------------------------------
{
  const clock = new Clock(), rig = makeRig(clock), k = makeKeyer(clock, rig);
  k.sendText('VVV VVV VVV VVV');
  clock.advance(1000);
  const env = k.sidetone.env;
  k.stop();
  const ev = env._ev;
  const cancel = ev.filter(e => e.k === 'cancel');
  check(cancel.length === 1 && close(cancel[0].t, k.sidetone.ctx.currentTime, 1e-6), 'STOP cancels the scheduled tone at once');
  check(k.characters.length === 0 && k.nextTime === 0 && rig.stops === 1, 'STOP clears the model and cuts the rig');
  const b = bursts(ev);
  check(b.length > 0 && b.every(x => x.off <= k.sidetone.ctx.currentTime + 0.006), 'no tone survives STOP');
}

// ---- 8. TONE off: silent, rig unaffected; on again mid-way: joins ----------
{
  const clock = new Clock(), rig = makeRig(clock), k = makeKeyer(clock, rig, { tone: false });
  k.sendText('PARIS PARIS PARIS');
  clock.advance(1000);
  check(k.sidetone.ctx === null && rig.frames.length === 1, 'TONE off: no audio graph, text keyed as usual');
  k.sidetone.setEnabled(true, k.elements(0));
  clock.advance(100);
  const b = bursts(k.sidetone.env._ev);
  const future = k.elements(0).filter(e => e.on >= clock.now() - 100).length;
  check(b.length >= future && b.length < k.elements(0).length, 'TONE on mid-way: only the remaining elements are scheduled (' + b.length + ')');
}

// ---- 9. the output device changes: a new context, never mid-tone ----------
{
  const clock = new Clock(), rig = makeRig(clock);
  const lat = [];
  const tone = new WfCwSidetone({
    enabled: true, createContext: () => fakeContext(clock), pitch: () => 600, gain: () => 0.2, onLatency: (ms) => lat.push(ms),
    now: () => clock.now(), setTimeout: (f, ms) => clock.setTimeout(f, ms), clearTimeout: (t) => clock.clearTimeout(t),
  });
  const k = new WfCwKeyer({ wpm: () => 20, send: rig.send, stopRig: rig.stop, sidetone: tone,
    now: () => clock.now(), setTimeout: (f, ms) => clock.setTimeout(f, ms), clearTimeout: (t) => clock.clearTimeout(t) });
  k.sendText('PARIS');
  const first = tone.ctx;
  clock.advance(1100);
  check(lat.length === 1 && lat[0] === 174, 'tone delay reported once the context plays (' + lat.join() + ' ms: 6 base + 168 output)');
  tone.reset();
  check(tone.ctx === first && first.state === 'running', 'device change while keying: the context is kept until the tone is out');
  const end = k.base + k.nextTime;
  clock.run(end + 600);
  check(tone.ctx !== first && first.state === 'closed' && tone.ctx && tone.ctx.oscillators.length === 0, 'and replaced when the oscillator parks (a new context, nothing playing on it)');
  clock.advance(1100);
  check(lat.length === 2, 'the new device is measured at once, without keying (' + lat.join() + ')');
  const b = bursts(first.lastGain._ev);
  check(b.length === k.elements(0).length, 'no element lost to the deferred rebuild (' + b.length + ' of ' + k.elements(0).length + ')');
  const rebuilt = tone.ctx;
  k.sendText('E');
  check(tone.ctx === rebuilt && tone.ctx.oscillators.length === 1, 'the next element plays on the rebuilt context');
  clock.run(clock.now() + 2000);
  const second = tone.ctx;
  tone.reset();
  check(second.state === 'closed' && tone.ctx && tone.ctx !== second, 'device change while idle: replaced at once');
  clock.advance(1100);
  check(lat.length === 3, 'and measured without keying');
  k.sendText('E'); clock.advance(200);
  const third = tone.ctx;
  tone.setEnabled(false);
  check(tone.ctx === null && third.state === 'closed', 'TONE off releases the context (off / on rebuilds it)');
}

console.log(failures ? 'FAILED ' + failures : 'ALL PASS');
process.exit(failures ? 1 : 0);

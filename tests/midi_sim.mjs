// Drives resources/web-shared/midi.js under node with a fake MIDIAccess and
// a virtual clock: message parsing, the three knob encodings (WheelA with
// its speed-sensitive magnitude, WheelB, absolute slider), note on / off /
// velocity-0 gates, the two CTR2-MIDI presets (note 20 is a paddle in one, a
// button in the other), MIDI learn and persistence, hot-plug with a held
// paddle released on unplug, a browser without Web MIDI, and the tuner that
// coalesces knob ticks without losing any.  Exit code 1 on a failed check.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { Clock } from './cw_sim_harness.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const W = {};
new Function('window', fs.readFileSync(path.join(here, '../resources/web-shared/midi.js'), 'utf8'))(W);
const { WfMidi, WfMidiTuner, MIDI_PRESETS, parseMidi } = W;

let failures = 0;
function check(ok, msg) { console.log((ok ? 'PASS ' : 'FAIL ') + msg); if (!ok) failures++; }

function memStorage(init = {}) {
  const m = { ...init };
  return { getItem: (k) => (k in m ? m[k] : null), setItem: (k, v) => { m[k] = String(v); }, _m: m };
}
// MIDIAccess look-alike: inputs is a Map, ports get onmidimessage.
function fakeAccess(names) {
  const access = { inputs: new Map(), onstatechange: null };
  access.plug = (name) => { const p = { id: name, name, type: 'input', state: 'connected', onmidimessage: null }; access.inputs.set(name, p); access.onstatechange && access.onstatechange({ port: p }); return p; };
  access.unplug = (name) => { const p = access.inputs.get(name); p.state = 'disconnected'; access.inputs.delete(name); access.onstatechange && access.onstatechange({ port: p }); };
  for (const n of names) access.plug(n);
  access.send = (name, bytes) => { const p = access.inputs.get(name); p.onmidimessage && p.onmidimessage({ data: Uint8Array.from(bytes) }); };
  return access;
}
const CC = (n, v) => [0xB0, n, v], ON = (n, v = 127) => [0x90, n, v], OFF = (n) => [0x80, n, 0];

// A midi object with every CTR2 preset action recorded.
async function makeMidi(storage = memStorage({ wfMidiEnabled: '1' }), access = fakeAccess(['CTR2-MIDI'])) {
  const log = [];
  const m = new WfMidi({ requestAccess: () => Promise.resolve(access), storage });
  const levels = { afGain: 0.5 };
  for (const name of ['tuneFine', 'tuneCoarse']) m.register(name, { label: name, kind: 'rel', fn: (t) => log.push([name, t]) });
  for (const name of ['afGain', 'squelch', 'rfGain', 'cwWpm']) m.register(name, { label: name, kind: 'abs', get: () => levels[name] ?? 0, fn: (v) => { levels[name] = v; log.push([name, +v.toFixed(4)]); } });
  for (const name of ['bandDown', 'bandUp', 'modeDown', 'modeUp', 'cwBar', 'spanUp', 'preampAtt', 'nr', 'notch', 'tuner', 'cwCut', 'pttToggle'])
    m.register(name, { label: name, kind: 'btn', fn: () => log.push([name]) });
  for (const name of ['paddleLeft', 'paddleRight', 'straightKey', 'ptt']) m.register(name, { label: name, kind: 'gate', fn: (c) => log.push([name, c]) });
  if (m.enabled) await m.enable();
  return { m, log, access, storage, levels };
}

// ---- 1. parsing -------------------------------------------------------------
{
  const n = parseMidi([0x93, 20, 100]), z = parseMidi([0x90, 20, 0]), f = parseMidi([0x80, 20, 64]), c = parseMidi([0xB0, 100, 65]);
  check(n.type === 'note' && n.on && n.ch === 4 && z.on === false && f.on === false, 'NoteOn, NoteOn velocity 0 and NoteOff parsed, channel 1-based');
  check(c.type === 'cc' && c.num === 100 && c.value === 65 && parseMidi([0xF8]) === null && parseMidi([0xE0, 0, 64]) === null, 'CC parsed; clock and pitch bend ignored');
}

// ---- 2. no Web MIDI: silent ---------------------------------------------------
{
  const m = new WfMidi({ requestAccess: null, storage: memStorage() });
  let threw = false, res;
  try { res = await m.enable(); } catch (e) { threw = true; }
  check(!threw && res === false && m.status === 'unsupported', 'without requestMIDIAccess: enable() resolves false, status unsupported, no throw');
  const d = new WfMidi({ requestAccess: () => Promise.reject(new Error('denied')), storage: memStorage() });
  await d.enable();
  check(d.status === 'denied', 'permission refused: status denied');
}

// ---- 2b. watch(): a controller seen without a prompt, once the site holds the permission ----
{
  let asked = 0;
  const access = fakeAccess(['CTR2-MIDI']);
  const mk = (perm) => new WfMidi({ requestAccess: () => { asked++; return Promise.resolve(access); }, queryPermission: () => Promise.resolve(perm), storage: memStorage() });
  const p = mk('prompt');
  await p.watch();
  check(asked === 0 && p.devices.length === 0, 'watch(), permission not given yet: no access request (no prompt at load), no device known');
  const g = mk('granted'), log = [];
  g.register('bandDown', { label: 'BAND -', kind: 'btn', fn: () => log.push(1) });
  await g.watch();
  check(asked === 1 && g.devices.join() === 'CTR2-MIDI' && g.status === 'off' && !g.enabled, 'permission held, MIDI off: the controller is listed, MIDI stays off');
  access.send('CTR2-MIDI', ON(1));
  check(log.length === 0, 'and its messages are not acted on');
  access.unplug('CTR2-MIDI');
  check(g.devices.length === 0, 'unplugged while off: the list follows');
  access.plug('CTR2-MIDI');
  check(g.devices.join() === 'CTR2-MIDI', 'plugged back in: listed again');
  await g.enable(); access.send('CTR2-MIDI', ON(1));
  check(asked === 1 && log.length === 1 && g.status === 'ready', 'switched on: the same access, messages acted on');
  g.disable(); access.send('CTR2-MIDI', ON(1));
  check(log.length === 1 && g.devices.join() === 'CTR2-MIDI', 'switched off again: deaf to messages, still listed');
}

// ---- 3. CTR2 preset: knob encodings -----------------------------------------
{
  const { m, log, access } = await makeMidi();
  check(m.status === 'ready' && m.devices.join() === 'CTR2-MIDI', 'enabled from storage, device listed');
  access.send('CTR2-MIDI', CC(100, 65)); access.send('CTR2-MIDI', CC(100, 63));
  access.send('CTR2-MIDI', CC(100, 74)); access.send('CTR2-MIDI', CC(100, 14));
  check(JSON.stringify(log.splice(0)) === JSON.stringify([['tuneFine', 1], ['tuneFine', -1], ['tuneFine', 10], ['tuneFine', -50]]), 'CC 100 WheelA: 65 -> +1, 63 -> -1, speed tuning 74 -> +10, 14 -> -50');
  access.send('CTR2-MIDI', CC(101, 66));
  check(log.splice(0)[0].join() === 'tuneCoarse,2', 'CC 101 push-and-turn -> coarse tune');
  access.send('CTR2-MIDI', CC(102, 127)); access.send('CTR2-MIDI', CC(102, 0)); access.send('CTR2-MIDI', CC(106, 64));
  check(JSON.stringify(log.splice(0)) === JSON.stringify([['afGain', 1], ['afGain', 0], ['cwWpm', 0.5039]]), 'CC 102 / 106 SliderA: absolute 0..127 -> 0..1');
  m.setEncoding('tuneFine', 'wheelB');
  access.send('CTR2-MIDI', CC(100, 1)); access.send('CTR2-MIDI', CC(100, 126));
  check(JSON.stringify(log.splice(0)) === JSON.stringify([['tuneFine', 1], ['tuneFine', -1]]), 'WheelB: 1 -> +1, 126 -> -1');
  m.setEncoding('tuneFine', 'slider');
  access.send('CTR2-MIDI', CC(100, 40)); access.send('CTR2-MIDI', CC(100, 43)); access.send('CTR2-MIDI', CC(100, 41));
  check(JSON.stringify(log.splice(0)) === JSON.stringify([['tuneFine', 3], ['tuneFine', -2]]), 'slider on a tune action: ticks from the change, first value only sets the origin');
  m.setEncoding('afGain', 'wheelA');
  access.send('CTR2-MIDI', CC(102, 74));
  check(log.length === 1 && Math.abs(log[0][1] - (0 + 10 / 127)) < 1e-3, 'WheelA on a level: relative to its current value (' + JSON.stringify(log.splice(0)) + ')');
}

// ---- 4. CTR2 preset: buttons, paddles, PTT ----------------------------------
{
  const { m, log, access } = await makeMidi();
  for (const n of [1, 2, 3, 4, 5, 6, 11, 12, 13, 14, 15, 16]) access.send('CTR2-MIDI', ON(n));
  check(log.splice(0).map(x => x[0]).join() === 'bandDown,bandUp,modeDown,modeUp,cwBar,spanUp,preampAtt,nr,notch,tuner,cwCut,pttToggle', 'buttons 1-6 short, 11-16 long press');
  access.send('CTR2-MIDI', OFF(2));
  check(log.length === 0, 'a button\'s NoteOff does nothing');
  access.send('CTR2-MIDI', ON(20)); access.send('CTR2-MIDI', ON(21)); access.send('CTR2-MIDI', OFF(20)); access.send('CTR2-MIDI', [0x90, 21, 0]);
  check(JSON.stringify(log.splice(0)) === JSON.stringify([['paddleLeft', true], ['paddleRight', true], ['paddleLeft', false], ['paddleRight', false]]), 'paddles 20 / 21: NoteOn closed, NoteOff or velocity 0 open');
  access.send('CTR2-MIDI', ON(30)); access.send('CTR2-MIDI', OFF(30)); access.send('CTR2-MIDI', ON(31)); access.send('CTR2-MIDI', OFF(31));
  check(log.splice(0).map(x => x.join(':')).join() === 'straightKey:true,straightKey:false,ptt:true,ptt:false', 'extended paddle mode 30 / 31: straight key and PTT');
  access.send('CTR2-MIDI', ON(96));
  check(log.length === 0, 'note 96 unbound in the normal preset');
  access.send('CTR2-MIDI', [0x9F, 1, 100]);
  check(log.splice(0)[0][0] === 'bandDown', 'any channel accepted (omni)');
}

// ---- 5. Extended BTN preset ---------------------------------------------------
{
  const { m, log, access } = await makeMidi();
  m.applyPreset('ctr2ext');
  access.send('CTR2-MIDI', ON(20)); access.send('CTR2-MIDI', ON(25)); access.send('CTR2-MIDI', ON(30));
  check(log.splice(0).map(x => x[0]).join() === 'preampAtt,pttToggle', 'Extended BTN: 25-30 are the long presses, 20 is a (knob mode 2) button, not a paddle');
  access.send('CTR2-MIDI', ON(96)); access.send('CTR2-MIDI', ON(97)); access.send('CTR2-MIDI', ON(98)); access.send('CTR2-MIDI', ON(99));
  check(log.splice(0).map(x => x[0]).join() === 'paddleLeft,paddleRight,straightKey,ptt', 'Extended BTN: paddle jack on 96-99');
  access.send('CTR2-MIDI', CC(100, 65));
  check(log.splice(0)[0][0] === 'tuneFine', 'knob CCs unchanged');
  check(MIDI_PRESETS.ctr2.bindings.length === 6 + 12 + 4 && MIDI_PRESETS.ctr2ext.bindings.length === 6 + 12 + 4, 'presets: 6 knob CCs, 12 buttons, 4 jack notes');
}

// ---- 6. learn + persistence -------------------------------------------------
{
  const storage = memStorage({ wfMidiEnabled: '1' });
  const { m, log, access } = await makeMidi(storage);
  m.learn('bandUp');
  access.send('CTR2-MIDI', OFF(60));            // note-off: ignored while learning
  check(m.learning === 'bandUp', 'learn waits through a NoteOff');
  access.send('CTR2-MIDI', ON(60));
  check(m.learning === null && log.length === 0, 'learn binds on the NoteOn without firing the action');
  access.send('CTR2-MIDI', ON(60)); access.send('CTR2-MIDI', ON(2));
  check(log.splice(0).map(x => x[0]).join() === 'bandUp', 'note 60 is now BAND +, its old note 2 freed');
  m.learn('tuneFine');
  access.send('CTR2-MIDI', ON(61));              // a note can't drive the knob
  access.send('CTR2-MIDI', CC(20, 1));           // first value 1: WheelB
  check(JSON.stringify(m.bindingsOf('tuneFine')) === JSON.stringify([{ type: 'cc', num: 20, action: 'tuneFine', enc: 'wheelB' }]), 'learning a knob skips notes and guesses WheelB from a value of 1');
  m.learn('rfGain');
  access.send('CTR2-MIDI', CC(102, 90));         // the AF knob's CC
  check(m.bindingsOf('afGain').length === 0 && m.bindingsOf('rfGain')[0].num === 102 && m.bindingsOf('rfGain')[0].enc === 'slider', 'a learnt CC moves off the action that had it; levels learn as slider');
  m.clear('cwWpm');
  check(m.bindingsOf('cwWpm').length === 0, 'clear removes the binding');
  m.setOpt('fineStep', 50);
  const again = await makeMidi(storage);
  check(JSON.stringify(again.m.bindings) === JSON.stringify(m.bindings), 'map persists in localStorage (wfMidiMap)');
  check(again.m.opts.fineStep === 50, 'knob options persist (wfMidiOpts)');
  m.disable();
  check(storage._m.wfMidiEnabled === '0' && m.status === 'off', 'disable persists and stops');
  access.send('CTR2-MIDI', ON(60));
  check(log.length === 0, 'disabled: no actions');
  const fresh = new WfMidi({ requestAccess: () => Promise.resolve(fakeAccess([])), storage: memStorage() });
  check(!fresh.enabled && fresh.bindings.length === MIDI_PRESETS.ctr2.bindings.length, 'first run: off, CTR2 map loaded');
}

// ---- 7. hot-plug; a held paddle / PTT released on unplug ----------------------
{
  const access = fakeAccess([]);
  const { m, log } = await makeMidi(memStorage({ wfMidiEnabled: '1' }), access);
  check(m.status === 'ready' && m.devices.length === 0, 'enabled with no device');
  access.plug('CTR2-MIDI');
  check(m.devices.join() === 'CTR2-MIDI', 'plugged in: listed');
  access.send('CTR2-MIDI', ON(1));
  check(log.splice(0)[0][0] === 'bandDown', 'plugged in: messages arrive without re-enabling');
  access.send('CTR2-MIDI', ON(31)); access.send('CTR2-MIDI', ON(20));
  log.splice(0);
  access.unplug('CTR2-MIDI');
  const rel = log.splice(0).map(x => x.join(':')).sort().join();
  check(rel === 'paddleLeft:false,ptt:false' && m.devices.length === 0, 'unplugged with PTT and a paddle held: both released (' + rel + ')');
  access.plug('CTR2-MIDI'); access.send('CTR2-MIDI', ON(31));
  m.applyPreset('ctr2');
  check(log.splice(0).map(x => x.join(':')).join() === 'ptt:true,ptt:false', 'loading a preset releases a held gate');
}

// ---- 8. tuner: coalesced, nothing lost ----------------------------------------
{
  const clock = new Clock();
  let rigFreq = 14_074_000;
  const sent = [];
  const tuner = new WfMidiTuner({ getFreq: () => rigFreq, send: (hz) => sent.push({ t: clock.now(), hz }), now: () => clock.now(), setTimeout: (f, ms) => clock.setTimeout(f, ms) });
  tuner.add(10);
  check(sent.length === 1 && sent[0].hz === 14_074_010, 'first tick goes out at once');
  for (let i = 0; i < 100; i++) { clock.advance(3); tuner.add(500); }   // a fast spin: a tick every 3 ms, rig readback frozen
  clock.advance(100);
  const gaps = sent.slice(1).map((s, i) => s.t - sent[i].t);
  check(sent[sent.length - 1].hz === 14_074_010 + 100 * 500, 'fast spin: every tick counted (' + sent[sent.length - 1].hz + ')');
  check(gaps.every(g => g >= 40), 'at most one setFrequency per 40 ms (' + sent.length + ' sent for 100 ticks)');
  rigFreq = 14_200_000;   // tuned elsewhere; a pause longer than hold
  clock.advance(1000);
  tuner.add(-10);
  check(sent[sent.length - 1].hz === 14_199_990, 'after a pause the next step starts from the rig\'s frequency');
  clock.advance(1000);
  rigFreq = 14_074_123;
  tuner.add(1000, 1000);
  check(sent[sent.length - 1].hz === 14_075_000, 'coarse step snaps to whole kHz');
}

console.log(failures ? 'FAILED ' + failures : 'ALL PASS');
process.exit(failures ? 1 : 0);

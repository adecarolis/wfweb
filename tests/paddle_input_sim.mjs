// Drives resources/web-shared/paddle-panel.js under node: the paddle settings
// (defaults, persistence, the keyer options carried over from the MIDI
// window) and the USB keyboard adapter (VBand: the paddle contacts are the
// left and right Ctrl keys) with fake key events -- off by default, repeats
// ignored, other keys held back while a contact is down, a lost focus
// releasing what is held, straight-key mode, LEARN -- and a keyboard-keyed
// "CQ" decoded by the real keyer (cw-paddle.js).  Exit code 1 on a failure.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { Clock, makeRig, fakeContext } from './cw_sim_harness.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const W = {};
for (const f of ['cw-keyer.js', 'cw-paddle.js', 'paddle-panel.js']) new Function('window', fs.readFileSync(path.join(here, '../resources/web-shared', f), 'utf8'))(W);
const { WfPaddleSettings, WfKeyboardPaddle, WfCwPaddle, WfCwKeyer, WfCwSidetone, CW_MORSE, paddleKeyName } = W;

let failures = 0;
function check(ok, msg) { console.log((ok ? 'PASS ' : 'FAIL ') + msg); if (!ok) failures++; }

function memStorage(init = {}) {
  const m = { ...init };
  return { getItem: (k) => (k in m ? m[k] : null), setItem: (k, v) => { m[k] = String(v); }, _m: m };
}
// EventTarget look-alike recording preventDefault.
function fakeTarget() {
  const l = {};
  return {
    addEventListener(t, f) { (l[t] = l[t] || []).push(f); },
    fire(type, code, extra = {}) {
      const e = { type, code, repeat: false, prevented: false, stopped: false, ...extra,
        preventDefault() { this.prevented = true; }, stopPropagation() { this.stopped = true; } };
      for (const f of l[type] || []) f(e);
      return e;
    },
  };
}
function makeKb(opts = {}) {
  const target = fakeTarget(), contacts = [];
  const settings = new WfPaddleSettings({ storage: opts.storage || memStorage() });
  let active = opts.active !== false;
  const kb = new WfKeyboardPaddle({ settings, target, onContact: (r, c) => contacts.push(r + ':' + c), isActive: () => active });
  return { kb, settings, target, contacts, setActive: (a) => { active = a; } };
}

// ---- 1. settings -------------------------------------------------------------
{
  const s = new WfPaddleSettings({ storage: memStorage() });
  check(s.get('keyerMode') === 'B' && s.get('swap') === false && s.get('keyboard') === false && s.get('keyLeft') === 'ControlLeft' && s.get('keyRight') === 'ControlRight',
    'defaults: iambic B, no swap, keyboard adapter off, left / right Ctrl');
  const st = memStorage({ wfMidiOpts: JSON.stringify({ keyerMode: 'A', swap: true, fineStep: 50 }) });
  const m = new WfPaddleSettings({ storage: st });
  check(m.get('keyerMode') === 'A' && m.get('swap') === true, 'keyer options carried over from the MIDI window (wfMidiOpts)');
  m.set('keyboard', true);
  const again = new WfPaddleSettings({ storage: st });
  check(again.get('keyboard') === true && again.get('keyerMode') === 'A' && JSON.parse(st._m.wfPaddleOpts).keyboard === true, 'settings persist in wfPaddleOpts');
  let n = 0; m.subscribe(() => n++); m.set('swap', false);
  check(n === 1, 'a change notifies subscribers');
}

// ---- 2. keyboard adapter --------------------------------------------------------
{
  const { kb, settings, target, contacts } = makeKb();
  let e = target.fire('keydown', 'ControlLeft');
  check(contacts.length === 0 && !e.prevented, 'adapter off (default): Ctrl stays an ordinary key');
  settings.set('keyboard', true);
  e = target.fire('keydown', 'ControlLeft');
  target.fire('keydown', 'ControlLeft', { repeat: true });
  const up = target.fire('keyup', 'ControlLeft');
  check(contacts.join() === 'left:true,left:false' && e.prevented && up.prevented, 'left Ctrl: left contact closed / opened, key repeat ignored');
  contacts.length = 0;
  target.fire('keydown', 'ControlRight'); target.fire('keyup', 'ControlRight');
  check(contacts.join() === 'right:true,right:false', 'right Ctrl: right contact');
  contacts.length = 0;
  target.fire('keydown', 'ControlLeft');
  const w = target.fire('keydown', 'KeyW');
  target.fire('keyup', 'ControlLeft');
  const w2 = target.fire('keydown', 'KeyW');
  check(w.prevented && w.stopped && !w2.prevented, 'a key pressed while a contact is down is held back (no Ctrl+W mid-character); not afterwards');
  contacts.length = 0;
  target.fire('keydown', 'ControlLeft'); target.fire('keydown', 'ControlRight');
  target.fire('blur', '');
  check(contacts.join() === 'left:true,right:true,left:false,right:false', 'a lost focus releases both contacts');
}
{
  const { settings, target, contacts, setActive } = makeKb({ active: false });
  settings.set('keyboard', true);
  const e = target.fire('keydown', 'ControlLeft');
  const c = target.fire('keydown', 'KeyC');
  target.fire('keyup', 'ControlLeft');
  check(contacts.join() === 'left:true,left:false' && !e.prevented && !c.prevented, 'outside CW: the contact is still reported (lamps), Ctrl+C left alone');
  setActive(true);
}
{
  const { kb, settings, target, contacts } = makeKb();
  settings.set('keyboard', true); settings.set('keyMode', 'straight');
  target.fire('keydown', 'ControlLeft'); target.fire('keyup', 'ControlLeft');
  target.fire('keydown', 'ControlRight');
  check(contacts.join() === 'straight:true,straight:false', 'straight-key mode: the left key is the key, the right one is ignored');
}
{
  const { kb, settings, target, contacts } = makeKb();
  kb.learn('keyLeft');   // with USB KEYS still off
  const e = target.fire('keydown', 'BracketLeft');
  check(settings.get('keyLeft') === 'BracketLeft' && kb.learning === null && contacts.length === 0 && e.prevented, 'LEARN binds the next key without keying it');
  check(settings.get('keyboard') === true, 'and switches USB KEYS on: a learnt key is one to use');
  target.fire('keydown', 'BracketLeft'); target.fire('keyup', 'BracketLeft');
  check(contacts.join() === 'left:true,left:false', 'and that key is now the left contact');
  check(paddleKeyName('ControlLeft') === 'Left Ctrl' && paddleKeyName('ControlRight') === 'Right Ctrl' && paddleKeyName('KeyX') === 'X', 'key names for the window');
}

// ---- 2b. Safari: a Ctrl release lost while the other Ctrl is held ------------
// Both Ctrl keys are one Control flag on a Mac; WebKit can drop (or put on
// the wrong side) the release of the first one let go.  Every event carries
// the modifier state, and a contact whose modifier is no longer down is
// released.  `ctrl` is getModifierState('Control') after the event.
{
  const ev = (t, type, code, ctrl) => t.fire(type, code, { getModifierState: (m) => m === 'Control' && ctrl });
  // a. the dah's release never arrives; the dit's keyup says no Ctrl is down
  let { settings, target, contacts, kb } = makeKb();
  settings.set('keyboard', true);
  ev(target, 'keydown', 'ControlRight', true);
  ev(target, 'keydown', 'ControlLeft', true);
  ev(target, 'keyup', 'ControlLeft', false);            // right was let go first: no event for it
  check(contacts.join() === 'right:true,left:true,left:false,right:false' && !kb.held.left && !kb.held.right,
    'lost release: the last Ctrl keyup (no Control down) releases the stuck dah too (' + contacts.join() + ')');
  // b. the release is reported on the wrong side
  ({ settings, target, contacts, kb } = makeKb());
  settings.set('keyboard', true);
  ev(target, 'keydown', 'ControlRight', true);
  ev(target, 'keydown', 'ControlLeft', true);
  ev(target, 'keyup', 'ControlLeft', true);             // the dah let go, reported as the dit
  ev(target, 'keyup', 'ControlLeft', false);            // the dit let go: no Control down
  check(!kb.held.left && !kb.held.right && contacts[contacts.length - 1] === 'right:false',
    'wrong side: still nothing held once no Ctrl is down (' + contacts.join() + ')');
  // c. Chromium-correct events are unchanged
  ({ settings, target, contacts, kb } = makeKb());
  settings.set('keyboard', true);
  ev(target, 'keydown', 'ControlRight', true);
  ev(target, 'keydown', 'ControlLeft', true);
  ev(target, 'keyup', 'ControlRight', true);
  ev(target, 'keyup', 'ControlLeft', false);
  check(contacts.join() === 'right:true,left:true,right:false,left:false', 'correct events: released in the order they come');
  check(kb.trace.length === 4 && /Right Ctrl/.test(kb.trace[0]) && /Control still down/.test(kb.trace[2]), 'the key monitor records them (' + kb.trace.join(' | ') + ')');
}

// ---- 3. a keyboard-keyed CQ through the real keyer -------------------------------
{
  const clock = new Clock(), rig = makeRig(clock);
  const T = { now: () => clock.now(), setTimeout: (f, ms) => clock.setTimeout(f, ms), clearTimeout: (t) => clock.clearTimeout(t) };
  const tone = new WfCwSidetone({ enabled: true, createContext: () => fakeContext(clock), pitch: () => 600, gain: () => 0.2, ...T });
  const keyer = new WfCwKeyer({ wpm: () => 20, send: rig.send, stopRig: rig.stop, sidetone: tone, ...T });
  let text = '';
  const paddle = new WfCwPaddle({ dot: () => keyer.dot(), sidetone: tone, ...T, onChar: (ch) => { text += ch; keyer.sendText(ch, { tone: false }); } });
  const settings = new WfPaddleSettings({ storage: memStorage() });
  settings.set('keyboard', true);
  const tgt = fakeTarget();   // contacts go straight to the keyer: the page's paddleContact minus its CW gate
  new WfKeyboardPaddle({ settings, target: tgt, onContact: (role, closed) => paddle.paddle(role, closed) });
  const d = keyer.dot();
  let prevOff = null, t0 = clock.now() + 100;
  for (const ch of 'CQ') {
    const p = CW_MORSE[ch];
    for (let k = 0; k < p.length; k++) {
      const start = k === 0 ? (prevOff === null ? t0 : prevOff + 3 * d) : prevOff + d, press = k === 0 ? start : prevOff + 0.5 * d;
      const len = (p[k] === '.' ? 1 : 3) * d, code = p[k] === '.' ? 'ControlLeft' : 'ControlRight';
      clock.run(press); tgt.fire('keydown', code);
      clock.run(start + 0.5 * len); tgt.fire('keyup', code);
      prevOff = start + len;
    }
  }
  clock.run(prevOff + 40 * d);
  const sent = rig.frames.filter(f => f.text !== null).map(f => f.text).join('');
  check(text === 'CQ ' && sent === 'CQ ', 'CQ keyed on left / right Ctrl is decoded and reaches the rig keyer (' + JSON.stringify(sent) + ')');
}

console.log(failures ? 'FAILED ' + failures : 'ALL PASS');
process.exit(failures ? 1 : 0);

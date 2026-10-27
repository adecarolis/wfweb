// In-browser check of the MIDI controller support (midi.js, cw-paddle.js).
//
// Builds the Standalone bundle, serves it on a free port and opens it on its
// in-browser virtual rig (?virtual=1: no hardware, nothing radiated) with a
// fake navigator.requestMIDIAccess standing in for a CTR2-MIDI.  Checks:
//   - FUNC > MIDI opens the window, ON finds the controller, LEARN binds;
//   - CC 100 / 101 (WheelA) tune the VFO by the fine / coarse step;
//   - button 2 steps the band, CC 102 (SliderA) sets AF gain;
//   - in CW, a paddle-contact timeline spelling "CQ" is keyed by the browser
//     keyer, decoded, and handed to the rig's keyer as sendCW text, shown in
//     the CW transcript; outside CW the paddle does nothing;
//   - the CW bar's BK-IN button cycles OFF / SEMI / FULL and fits a phone.
// Needs playwright-core and a Chromium (found like cw_bench.mjs); `--probe`
// exits 0 when both are found and 3 otherwise.  WFWEB_SHOT_DIR keeps a
// screenshot of the MIDI window.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import http from 'node:http';
import { execFileSync } from 'node:child_process';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(here, '..');

function findBrowser() {
  const home = os.homedir();
  let core = process.env.WFWEB_PLAYWRIGHT_CORE || null;
  if (!core) {
    const cands = [];
    for (const base of [path.join(home, '.npm', '_npx'), path.join(ROOT, 'node_modules')]) {
      if (!fs.existsSync(base)) continue;
      const dirs = base.endsWith('node_modules') ? [base] : fs.readdirSync(base).map(d => path.join(base, d, 'node_modules'));
      for (const d of dirs) {
        const p = path.join(d, 'playwright-core');
        if (fs.existsSync(path.join(p, 'package.json'))) cands.push({ p, v: JSON.parse(fs.readFileSync(path.join(p, 'package.json'))).version });
      }
    }
    cands.sort((a, b) => b.v.localeCompare(a.v, undefined, { numeric: true }));
    if (cands.length) core = cands[0].p;
  }
  let chrome = process.env.WFWEB_CHROMIUM || null;
  if (!chrome) {
    const base = path.join(home, '.cache', 'ms-playwright');
    if (fs.existsSync(base)) {
      const revs = fs.readdirSync(base).filter(d => /^chromium-\d+$/.test(d)).sort((a, b) => parseInt(b.slice(9)) - parseInt(a.slice(9)));
      for (const r of revs) {
        for (const sub of ['chrome-linux64/chrome', 'chrome-linux/chrome']) {
          const p = path.join(base, r, sub);
          if (fs.existsSync(p)) { chrome = p; break; }
        }
        if (chrome) break;
      }
    }
    if (!chrome) for (const p of ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser']) if (fs.existsSync(p)) { chrome = p; break; }
  }
  return core && chrome ? { core, chrome } : null;
}

const found = findBrowser();
if (process.argv.includes('--probe')) { console.log(found ? `playwright-core ${found.core}\nchromium ${found.chrome}` : 'no browser'); process.exit(found ? 0 : 3); }
if (!found) { console.error('no playwright-core / Chromium found'); process.exit(3); }
const { chromium } = createRequire(import.meta.url)(found.core);

let fails = 0;
function check(ok, msg) { console.log((ok ? 'PASS ' : 'FAIL ') + msg); if (!ok) fails++; }

// ---- build + serve the Standalone bundle -----------------------------------
const dist = fs.mkdtempSync(path.join(os.tmpdir(), 'wfweb-midi-'));
execFileSync('bash', [path.join(ROOT, 'tools', 'build-static.sh'), dist], { stdio: 'ignore' });
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css', '.wasm': 'application/wasm', '.svg': 'image/svg+xml', '.png': 'image/png', '.json': 'application/json', '.onnx': 'application/octet-stream' };
const server = http.createServer((req, res) => {
  let p = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  if (p.endsWith('/')) p += 'index.html';
  const f = path.join(dist, path.normalize(p));
  if (!f.startsWith(dist) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': TYPES[path.extname(f)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
  fs.createReadStream(f).pipe(res);
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const URL0 = `http://127.0.0.1:${server.address().port}/?virtual=1&id=1`;

// ---- fake Web MIDI: one CTR2-MIDI input ------------------------------------
// The MIDI permission reads 'prompt' (never given on this site) unless the
// argument says 'granted'.
function fakeMidi(perm) {
  const port = { id: 'ctr2', name: 'CTR2-MIDI', manufacturer: 'Lynovation', type: 'input', state: 'connected', onmidimessage: null };
  const access = { inputs: new Map([['ctr2', port]]), outputs: new Map(), onstatechange: null };
  window.__midiRequests = 0;
  Object.defineProperty(navigator, 'requestMIDIAccess', { configurable: true, value: () => { window.__midiRequests++; return Promise.resolve(access); } });
  const query = navigator.permissions.query.bind(navigator.permissions);
  navigator.permissions.query = (d) => d && d.name === 'midi' ? Promise.resolve({ state: perm || 'prompt' }) : query(d);
  window.__midiSend = (bytes) => { if (port.onmidimessage) port.onmidimessage({ data: Uint8Array.from(bytes), timeStamp: performance.now() }); };
  window.__midiPlug = (on) => {
    port.state = on ? 'connected' : 'disconnected';
    if (on) access.inputs.set('ctr2', port); else access.inputs.delete('ctr2');
    if (access.onstatechange) access.onstatechange({ port });
  };
}

const browser = await chromium.launch({
  executablePath: found.chrome, headless: true,
  args: ['--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream', '--autoplay-policy=no-user-gesture-required'],
});
try {
  const ctx = await browser.newContext({ viewport: { width: 1100, height: 800 } });
  await ctx.addInitScript(fakeMidi);
  const page = await ctx.newPage();
  page.on('pageerror', (e) => { console.log('pageerror: ' + e.message); fails++; });
  await page.goto(URL0);
  await page.click('#startBtn');
  await page.waitForFunction(() => window.currentFreq > 0, null, { timeout: 30000 });
  // Log every command the page sends to the rig.
  await page.evaluate(() => {
    window.__cmds = [];
    const orig = window.send;
    window.send = function (obj) { window.__cmds.push(JSON.parse(JSON.stringify(obj))); return orig.apply(this, arguments); };
  });
  const midi = (bytes) => page.evaluate((b) => window.__midiSend(b), bytes);
  const cmds = (name) => page.evaluate((n) => window.__cmds.filter(c => c.cmd === n), name);

  // -- first time (no permission yet): no tile; the window via PADDLE, ON, LEARN --
  check(await page.evaluate(() => window.__midiRequests === 0 && !window.midi.enabled), 'MIDI off by default: no access requested at load');
  await page.evaluate(() => window.setPanel('func'));
  const tile = page.locator('#funcGrid .ov-btn', { hasText: 'MIDI' });
  check(await tile.count() === 0, 'permission never given: no controller can be seen, no MIDI tile');
  await page.evaluate(() => { window.setPanel('default'); window.openPaddleModal(); });
  await page.locator('#paddlePanel .wf-btn', { hasText: 'MIDI' }).click();
  check(await page.locator('#midiOverlay.visible').count() === 1, 'MIDI window opens from PADDLE > MIDI...');
  await page.locator('#midiPanel .midi-row .wf-btn', { hasText: 'OFF' }).first().click();
  await page.waitForFunction(() => window.midi.status === 'ready');
  check((await page.locator('#midiPanel .midi-status').textContent()).includes('CTR2-MIDI'), 'ON: the controller is listed');
  await page.evaluate(() => window.setPanel('func'));
  check(await tile.count() === 1 && (await tile.textContent()).includes('ON'), 'the controller found: FUNC has a MIDI tile, ON');
  await page.evaluate(() => window.__midiPlug(false));
  check(await tile.count() === 0, 'unplugged: the tile goes');
  await page.evaluate(() => window.__midiPlug(true));
  check(await tile.count() === 1, 'plugged back in: the tile is back');
  await page.evaluate(() => window.setPanel('default'));
  const spanRow = page.locator('#midiPanel .midi-map-row', { hasText: 'SPAN -' });
  await spanRow.locator('.wf-btn', { hasText: 'LEARN' }).click();
  await midi([0x90, 70, 127]);
  await page.waitForFunction(() => !window.midi.learning);
  check((await spanRow.locator('.midi-binding').textContent()) === 'Note 70', 'LEARN binds SPAN - to the next note (Note 70)');
  check((await page.locator('#midiPanel .midi-monitor').textContent()).includes('Note 70 on (ch 1) \u2192 SPAN -'), 'monitor shows the last message and the action it drives');
  await midi([0x90, 99, 127]);
  check((await page.locator('#midiPanel .midi-monitor').textContent()).endsWith('unmapped'), 'an unbound message reads "unmapped"');
  if (process.env.WFWEB_SHOT_DIR) await page.locator('#midiOverlay').screenshot({ path: path.join(process.env.WFWEB_SHOT_DIR, 'midi-window.png') });
  await page.evaluate(() => { window.closeMidiModal(); window.setPanel('default'); });
  check(await page.evaluate(() => localStorage.getItem('wfMidiEnabled') === '1' && JSON.parse(localStorage.getItem('wfMidiMap')).bindings.some(b => b.num === 70)), 'enabled flag and learnt map persisted');

  // -- permission already held, MIDI off: the tile shows at load, OFF --
  {
    const ctx3 = await browser.newContext();
    await ctx3.addInitScript(fakeMidi, 'granted');
    const p3 = await ctx3.newPage();
    await p3.goto(URL0);
    await p3.waitForFunction(() => typeof window.midi === 'object' && window.midi.devices.length > 0, null, { timeout: 10000 }).catch(() => {});
    await p3.evaluate(() => window.setPanel('func'));
    const t3 = p3.locator('#funcGrid .ov-btn', { hasText: 'MIDI' });
    check(await t3.count() === 1 && (await t3.textContent()).includes('OFF') && await p3.evaluate(() => !window.midi.enabled && window.__midiRequests === 1),
      'permission held: the controller is seen at load without switching MIDI on; tile reads OFF');
    await ctx3.close();
  }

  // -- a browser without Web MIDI (Safari): no tile, the window says why --
  {
    const ctx2 = await browser.newContext();
    await ctx2.addInitScript(() => { delete Navigator.prototype.requestMIDIAccess; });
    const p2 = await ctx2.newPage();
    await p2.goto(URL0);
    await p2.waitForFunction(() => typeof window.midi === 'object');
    await p2.evaluate(() => window.setPanel('func'));
    const t2 = p2.locator('#funcGrid .ov-btn', { hasText: 'MIDI' });
    check(await t2.count() === 0, 'no Web MIDI: no MIDI tile');
    await p2.evaluate(() => window.openMidiModal());   // the start splash still covers the page here
    check((await p2.locator('#midiPanel').textContent()).includes('Chrome or Edge'), 'and the MIDI window says to use Chrome or Edge');
    await ctx2.close();
  }

  // -- knob: fine and coarse tune --
  const f0 = await page.evaluate(() => window.currentFreq);
  await midi([0xB0, 100, 74]);   // WheelA +10 ticks x 10 Hz
  await page.waitForFunction((f) => window.currentFreq === f + 100, f0, { timeout: 5000 }).catch(() => {});
  check(await page.evaluate(() => window.currentFreq) === f0 + 100, `CC 100 = 74: VFO +100 Hz (${f0} -> ${await page.evaluate(() => window.currentFreq)})`);
  await page.waitForTimeout(600);   // past the tuner's hold
  await midi([0xB0, 101, 66]);   // +2 ticks x 1 kHz, snapped
  const want = Math.round((f0 + 100 + 2000) / 1000) * 1000;
  await page.waitForFunction((w) => window.currentFreq === w, want, { timeout: 5000 }).catch(() => {});
  check(await page.evaluate(() => window.currentFreq) === want, `CC 101 = 66: VFO +2 kHz on whole kHz (${await page.evaluate(() => window.currentFreq)}, want ${want})`);

  // -- button 2: band up --
  const bandBefore = await page.evaluate(() => window.bandLabelOf(window.currentFreq));
  await midi([0x90, 2, 127]);
  await page.waitForFunction((b) => window.bandLabelOf(window.currentFreq) !== b, bandBefore, { timeout: 5000 }).catch(() => {});
  const bandAfter = await page.evaluate(() => window.bandLabelOf(window.currentFreq));
  check(bandAfter && bandAfter !== bandBefore, `button 2: band up (${bandBefore} -> ${bandAfter})`);

  // -- CC 102: AF gain --
  await midi([0xB0, 102, 127]);
  await page.waitForTimeout(200);
  check(await page.evaluate(() => document.getElementById('afGainSlider').value) === '255', 'CC 102 = 127: AF slider at 255');
  check((await cmds('setAfGain')).some(c => c.value === 255), 'and setAfGain 255 sent');

  // -- paddle outside CW: ignored --
  await page.evaluate(() => window.setMode('USB'));
  await page.waitForFunction(() => window.currentMode === 'USB', null, { timeout: 10000 });
  await midi([0x90, 20, 127]); await page.waitForTimeout(80); await midi([0x80, 20, 0]);
  await page.waitForTimeout(600);
  check((await cmds('sendCW')).length === 0 && await page.evaluate(() => document.getElementById('cwBar').classList.contains('hidden')), 'paddle in USB: nothing keyed, CW bar stays shut');

  // -- paddle in CW: CQ --
  await page.evaluate(() => { window.setMode('CW'); window.setCwSpeedValue(12); });
  await page.waitForFunction(() => window.currentMode === 'CW', null, { timeout: 10000 });
  // Contacts scheduled inside the page (in-page timers, not CDP round trips):
  // the first element of a character pressed when the keyer is idle, the
  // next ones half-way through the gap before them, each released half-way
  // through its element (as tests/cw_paddle_sim.mjs).
  const dot = await page.evaluate(() => window.cwKeyer.dot());
  await page.evaluate((dot) => new Promise((done) => {
    const M = window.CW_MORSE, ev = [];
    let prevOff = null, t0 = 50;
    for (const ch of 'CQ') {
      const p = M[ch];
      for (let k = 0; k < p.length; k++) {
        const start = k === 0 ? (prevOff === null ? t0 : prevOff + 3 * dot) : prevOff + dot;
        const press = k === 0 ? start : prevOff + 0.5 * dot;
        const len = (p[k] === '.' ? 1 : 3) * dot, note = p[k] === '.' ? 20 : 21;
        ev.push([press, [0x90, note, 127]], [start + 0.5 * len, [0x80, note, 0]]);
        prevOff = start + len;
      }
    }
    for (const [t, b] of ev) setTimeout(() => window.__midiSend(b), t);
    setTimeout(done, prevOff + 10 * dot);
  }), dot);
  await page.waitForTimeout(1500);
  const sent = (await cmds('sendCW')).map(c => c.text).join('');
  check(sent === 'CQ ', `paddle "CQ" at 12 WPM reaches the rig keyer as sendCW (${JSON.stringify(sent)})`);
  check(!(await page.evaluate(() => document.getElementById('cwBar').classList.contains('hidden'))), 'the paddle opened the CW bar');
  check((await page.locator('#cwDisplay').textContent()).replace(/\s/g, '') === 'CQ', 'CW transcript shows CQ');
  check(await page.evaluate(() => !!(window.cwSidetone.ctx)), 'paddle sidetone started');
  check(await page.evaluate(() => window.cwKeyer.characters.every(c => c.noTone)), 'paddle text carries no second tone');

  // -- BK-IN in the CW bar: OFF -> BKIN -> F-BKIN, from the rig's reports --
  const bk = page.locator('#cwBreakInBtn');
  const bkLabel = () => bk.locator('.cw-wide').textContent();
  check(await bk.isVisible() && await bkLabel() === 'BKIN', 'BK-IN button in the CW bar, reading BKIN (the virtual rig starts in semi break-in)');
  const bkSeen = [];
  for (let i = 0; i < 3; i++) { await bk.click(); await page.waitForTimeout(150); bkSeen.push(await bkLabel() + (await bk.evaluate(b => b.classList.contains('active')) ? '*' : '')); }
  check(bkSeen.join() === 'F-BKIN*,BK-IN,BKIN*', 'BK-IN cycles like the radio key: ' + bkSeen.join(' -> ') + ' (* = lit)');
  check((await cmds('setBreakIn')).map(c => c.value).join() === '2,0,1', 'and sends setBreakIn 2, 0, 1');
  await page.setViewportSize({ width: 390, height: 844 });
  await page.waitForTimeout(300);
  const hdr = await page.evaluate(() => {
    const h = document.querySelector('#cwBar .cw-bar-header');
    const c = document.getElementById('cwCloseBtn').getBoundingClientRect();
    return { fits: h.scrollWidth <= h.clientWidth + 1, close: Math.round(c.width), bk: Math.round(document.getElementById('cwBreakInBtn').getBoundingClientRect().width) };
  });
  check(hdr.fits && hdr.close >= 28 && hdr.bk >= 24, `phone width: the CW header still fits with BK-IN (✕ ${hdr.close} px, BK ${hdr.bk} px)`);
  const row = await page.evaluate(() => {
    const r = document.querySelector('#cwBar .cw-callsign-row'), b = document.getElementById('cwPaddleBtn').getBoundingClientRect();
    return { fits: r.scrollWidth <= r.clientWidth + 1, w: Math.round(b.width), right: Math.round(b.right), vw: innerWidth };
  });
  check(row.fits && row.w >= 24 && row.right <= row.vw, `phone width: PADDLE fits the CALL row (${row.w} px wide, right edge ${row.right} of ${row.vw})`);
  await page.setViewportSize({ width: 1100, height: 800 });
  await page.waitForTimeout(300);

  // -- PADDLE window: keyer options moved here, USB keyboard adapter (VBand) --
  check(!(await page.evaluate(() => { window.openMidiModal(); const t = document.getElementById('midiPanel').textContent; window.closeMidiModal(); return /IAMBIC/.test(t); })), 'the MIDI window no longer carries the keyer options');
  await page.click('#cwPaddleBtn');
  check(await page.locator('#paddleOverlay.visible').count() === 1, 'CW bar > PADDLE opens the paddle window');
  const pp = page.locator('#paddlePanel');
  check(/IAMBIC B/.test(await pp.textContent()) && /Left Ctrl/.test(await pp.textContent()) && /Right Ctrl/.test(await pp.textContent()), 'it shows the keyer and the adapter keys (left / right Ctrl)');
  const usbOff = pp.locator('.midi-row', { hasText: 'USB KEYS' }).locator('.wf-btn', { hasText: 'OFF' });
  const cwBefore = (await cmds('sendCW')).length;
  await page.keyboard.down('ControlLeft'); await page.keyboard.up('ControlLeft');
  await page.waitForTimeout(800);
  check((await cmds('sendCW')).length === cwBefore, 'adapter off: a Ctrl key keys nothing');
  await usbOff.click();
  check(await page.evaluate(() => window.paddleSettings.get('keyboard') === true), 'USB KEYS on');
  await page.keyboard.down('ControlLeft');
  const lit = await page.evaluate(() => document.querySelector('#paddlePanel .paddle-lamp.on') && document.querySelector('#paddlePanel .paddle-lamp.on').textContent);
  await page.keyboard.up('ControlLeft');
  const unlit = await page.evaluate(() => !document.querySelector('#paddlePanel .paddle-lamp.on'));
  check(/^LEFT/.test(lit || '') && unlit, 'a real left-Ctrl key event lights the LEFT lamp while held (' + lit + ')');
  await pp.locator('.midi-row', { hasText: 'RIGHT' }).locator('.wf-btn', { hasText: 'LEARN' }).click();
  await page.keyboard.press('BracketRight');
  check(await page.evaluate(() => window.paddleSettings.get('keyRight') === 'BracketRight') && /BracketRight/.test(await pp.textContent()), 'LEARN binds the next key (] for the right contact)');
  await page.evaluate(() => window.paddleSettings.set('keyRight', 'ControlRight'));
  await page.evaluate(() => window.closePaddleModal());
  await page.waitForTimeout(2500);   // the dit above (and its space) are keyed and gone
  const before = (await cmds('sendCW')).map(c => c.text).join('');
  // "CQ" on the Ctrl keys, timed in the page like the MIDI timeline above
  await page.evaluate((dot) => new Promise((done) => {
    const M = window.CW_MORSE, ev = [];
    let prevOff = null;
    for (const ch of 'CQ') {
      const p = M[ch];
      for (let k = 0; k < p.length; k++) {
        const start = k === 0 ? (prevOff === null ? 50 : prevOff + 3 * dot) : prevOff + dot;
        const press = k === 0 ? start : prevOff + 0.5 * dot;
        const len = (p[k] === '.' ? 1 : 3) * dot, code = p[k] === '.' ? 'ControlLeft' : 'ControlRight';
        ev.push([press, 'keydown', code], [start + 0.5 * len, 'keyup', code]);
        prevOff = start + len;
      }
    }
    for (const [t, type, code] of ev) setTimeout(() => window.dispatchEvent(new KeyboardEvent(type, { code, key: 'Control', ctrlKey: type === 'keydown', bubbles: true })), t);
    setTimeout(done, prevOff + 10 * dot);
  }), dot);
  await page.waitForTimeout(1500);
  const after = (await cmds('sendCW')).map(c => c.text).join('').slice(before.length);
  check(after === 'CQ ', `"CQ" on left / right Ctrl reaches the rig keyer (${JSON.stringify(after)})`);
} finally {
  await browser.close();
  server.close();
  fs.rmSync(dist, { recursive: true, force: true });
}

console.log(fails ? 'FAILED ' + fails : 'ALL PASS');
process.exit(fails ? 1 : 0);

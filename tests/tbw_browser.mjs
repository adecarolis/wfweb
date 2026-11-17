// The SSB transmit filter (TBW) held at WIDE for wfweb's own digital overs,
// in a real browser, Standalone build, never a real radio.
//
// The Web Serial transport against a fake IC-7300 (navigator.serial stubbed,
// the fake answers CI-V). The fake keeps one TBW setting with COMP off and
// one with COMP on, as the radio does (16 58: 0 WIDE / 1 MID / 2 NAR).
// A digital key (setPTT {digital: true}) switches COMP off, reads 16 58,
// sets WIDE; the unkey puts the filter back, then COMP. A voice key touches
// neither. WIDE already: no set. COMP switched by the operator mid-over: the
// hold moves to the other setting. Then the FUNC TBW tile: it reads the
// setting of the COMP state in use, a tap steps and writes it, and it is
// gone outside SSB.
//
// Needs playwright-core and a Chromium (found like comp_browser.mjs);
// `--probe` only reports whether they were found. Run through
// tests/test_compressor.py.

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
const dist = fs.mkdtempSync(path.join(os.tmpdir(), 'wfweb-tbw-'));
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
const BASE = `http://127.0.0.1:${server.address().port}/`;

// A fake IC-7300 behind navigator.serial: answers the probe, frequency and
// mode, COMP (16 44), TBW (16 58, one setting per COMP state), and ACKs
// everything else, the key (1C 00) included.
function fakeIc7300() {
  const RIG = 0x94, CTL = 0xE0;
  const rig = window.__rig = { comp: 1, tbw: [2, 1], mode: 0x01, freq: 14074000 };
  const log = window.__civLog = [];
  let ctrl = null;
  const readable = new ReadableStream({ start(c) { ctrl = c; } });
  const reply = (bytes) => ctrl.enqueue(new Uint8Array([0xFE, 0xFE, CTL, RIG, ...bytes, 0xFD]));
  const bcd = (hz) => { const out = []; for (let i = 0; i < 5; i++) { const two = hz % 100; hz = Math.floor(hz / 100); out.push(((two / 10 | 0) << 4) | (two % 10)); } return out; };
  const respond = (p) => {
    const [c, s] = p;
    if (c === 0x19) return reply([0x19, 0x00, RIG]);
    if (c === 0x03) return reply([0x03, ...bcd(rig.freq)]);
    if (c === 0x04) return reply([0x04, rig.mode, 0x01]);
    if (c === 0x06 && p.length >= 2) { rig.mode = p[1]; return reply([0xFB]); }
    if (c === 0x16 && s === 0x44) {
      if (p.length === 2) return reply([0x16, 0x44, rig.comp]);
      rig.comp = p[2] ? 1 : 0;
      return reply([0xFB]);
    }
    if (c === 0x16 && s === 0x58) {
      if (p.length === 2) return reply([0x16, 0x58, rig.tbw[rig.comp]]);
      if (p[2] > 2) return reply([0xFA]);
      rig.tbw[rig.comp] = p[2];
      return reply([0xFB]);
    }
    return reply([0xFB]);
  };
  let buf = [];
  const writable = new WritableStream({
    write(chunk) {
      for (const b of chunk) {
        buf.push(b);
        if (b !== 0xFD) continue;
        let i = 0;
        while (buf[i] === 0xFE) i++;
        const payload = buf.slice(i + 2, buf.length - 1);
        buf = [];
        if (!payload.length) continue;
        log.push(payload);
        setTimeout(() => respond(payload), 3);
      }
    },
  });
  const port = {
    readable, writable,
    open: async () => {}, close: async () => {},
    getInfo: () => ({ usbVendorId: 0x10C4, usbProductId: 0xEA60 }),
    addEventListener() {}, removeEventListener() {},
  };
  const serial = {
    getPorts: async () => [], requestPort: async () => port,
    addEventListener() {}, removeEventListener() {},
  };
  Object.defineProperty(Navigator.prototype, 'serial', { configurable: true, get: () => serial });
}

const browser = await chromium.launch({
  executablePath: found.chrome, headless: true,
  args: ['--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream', '--autoplay-policy=no-user-gesture-required'],
});

try {
  const ctx = await browser.newContext({ viewport: { width: 1100, height: 800 } });
  const page = await ctx.newPage();
  page.on('pageerror', (e) => { console.log('pageerror: ' + e.message); fails++; });
  await page.addInitScript(fakeIc7300);
  await page.goto(BASE);
  await page.click('#startBtn');
  await page.waitForFunction(() => window.currentFreq > 0, null, { timeout: 30000 });

  // The commands of interest since a mark, as text: COMP, TBW and the key.
  const NAMES = { '22 68 0': 'COMP off', '22 68 1': 'COMP on', '22 88': 'TBW?', '22 88 0': 'TBW WIDE', '22 88 1': 'TBW MID', '22 88 2': 'TBW NAR', '28 0 1': 'KEY', '28 0 0': 'UNKEY' };
  const mark = () => page.evaluate(() => window.__civLog.length);
  const since = (m) => page.evaluate(([m, names]) => window.__civLog.slice(m).map(p => names[p.join(' ')]).filter(Boolean), [m, NAMES]);
  const rig = () => page.evaluate(() => JSON.parse(JSON.stringify(window.__rig)));
  const send = (cmd) => page.evaluate((c) => window.send(c), cmd);
  const settle = () => page.waitForTimeout(400);
  const before = (list, a, b) => list.indexOf(a) >= 0 && list.indexOf(a) < list.indexOf(b);

  // The transport must know the mode and COMP before it holds anything.
  await page.waitForFunction(() => window.currentMode === 'USB', null, { timeout: 10000 });
  await page.waitForFunction(() => window.__civLog.some(p => p[0] === 0x16 && p[1] === 0x44), null, { timeout: 10000 });
  await settle();
  check(!(await since(0)).some(x => /^TBW [A-Z]/.test(x)), 'TBW is not set outside a digital over');
  // The reads of the tile (connect, slow tick, COMP change) are not the over's.
  const sinceSets = async (m) => (await since(m)).filter(x => x !== 'TBW?');
  const lastReadBefore = (list, what) => list.lastIndexOf('TBW?', list.indexOf(what));

  // ---- a digital over with COMP on, NAR in the COMP-off setting -------------
  for (const n of [1, 2]) {
    let m = await mark();
    await send({ cmd: 'setPTT', value: true, digital: true });
    await page.waitForFunction(() => window.__rig.tbw[0] === 0, null, { timeout: 5000 })
      .then(() => check(true, `over ${n}: the COMP-off filter goes to WIDE`),
            () => check(false, `over ${n}: the filter never went to WIDE`));
    let f = await since(m);
    check(f.includes('TBW WIDE') && lastReadBefore(f, 'TBW WIDE') > f.indexOf('COMP off') && f.includes('KEY'),
          `over ${n}: COMP off, then the read, then WIDE (got ${f.join(', ')})`);
    check((await rig()).tbw[1] === 1, `over ${n}: the COMP-on setting is left alone`);
    await send({ cmd: 'setPTT', value: false });
    await page.waitForFunction(() => window.__rig.comp === 1, null, { timeout: 5000 }).catch(() => {});
    await settle();
    f = await since(m);
    const r = await rig();
    check(r.comp === 1 && r.tbw.join() === '2,1', `over ${n}: NAR and COMP are back (got comp ${r.comp}, tbw ${r.tbw.join()})`);
    check(before(f, 'UNKEY', 'TBW NAR') && before(f, 'TBW NAR', 'COMP on'),
          `over ${n}: unkey, then NAR, then COMP on (got ${f.join(', ')})`);
  }

  // ---- a voice over ---------------------------------------------------------
  {
    const m = await mark();
    await send({ cmd: 'setPTT', value: true });
    await settle();
    await send({ cmd: 'setPTT', value: false });
    await settle();
    const f = await sinceSets(m);
    check(f.join() === 'KEY,UNKEY', `a voice over touches neither COMP nor the filter (got ${f.join(', ')})`);
  }

  // ---- WIDE already ---------------------------------------------------------
  {
    await page.evaluate(() => { window.__rig.tbw = [0, 2]; });
    const m = await mark();
    await send({ cmd: 'setPTT', value: true, digital: true });
    await settle();
    await send({ cmd: 'setPTT', value: false });
    await settle();
    const f = await since(m);
    check(f.includes('TBW?') && !f.some(x => /^TBW [A-Z]/.test(x)), `WIDE already: read, never set (got ${f.join(', ')})`);
    check((await rig()).tbw.join() === '0,2', 'WIDE already: both settings unchanged');
  }

  // ---- the operator switches COMP during the over ---------------------------
  {
    await send({ cmd: 'setCompressor', value: false });
    await page.waitForFunction(() => window.__rig.comp === 0, null, { timeout: 5000 });
    await page.evaluate(() => { window.__rig.tbw = [2, 1]; });
    await send({ cmd: 'setPTT', value: true, digital: true });
    await page.waitForFunction(() => window.__rig.tbw[0] === 0, null, { timeout: 5000 }).catch(() => {});
    await send({ cmd: 'setCompressor', value: true });
    await page.waitForFunction(() => window.__rig.comp === 1 && window.__rig.tbw.join() === '2,0', null, { timeout: 5000 })
      .then(() => check(true, 'COMP on mid-over: the COMP-off filter is back on NAR, the COMP-on one held at WIDE'),
            async () => check(false, `COMP on mid-over: got ${JSON.stringify(await rig())}`));
    await send({ cmd: 'setPTT', value: false });
    await settle();
    const r = await rig();
    check(r.comp === 1 && r.tbw.join() === '2,1', `after that over: COMP stays on, both filters as the operator had them (got comp ${r.comp}, tbw ${r.tbw.join()})`);
  }

  // ---- the FUNC TBW tile ----------------------------------------------------
  const tile = () => page.evaluate(() => {
    const t = [...document.querySelectorAll('#funcGrid .ov-btn')].find(b => (b.querySelector('.btn-label') || {}).textContent === 'TBW');
    return t ? t.querySelector('.btn-state').textContent : null;
  });
  const tapTile = () => page.evaluate(() => {
    [...document.querySelectorAll('#funcGrid .ov-btn')].find(b => (b.querySelector('.btn-label') || {}).textContent === 'TBW').click();
  });
  {
    await page.evaluate(() => window.setPanel('func'));
    await page.waitForFunction(() => window.txBandwidth === 1, null, { timeout: 8000 }).catch(() => {});
    check(await tile() === 'MID', `the tile reads MID, the COMP-on setting (got ${await tile()})`);
    const order = await page.evaluate(() => [...document.querySelectorAll('#funcGrid .ov-btn .btn-label')].map(e => e.textContent));
    check(order.indexOf('COMP') >= 0 && order.indexOf('COMP') + 1 === order.indexOf('TBW'), `the tile sits after COMP (got ${order.join(' ')})`);
    await send({ cmd: 'setCompressor', value: false });
    await page.waitForFunction(() => window.txBandwidth === 2, null, { timeout: 5000 }).catch(() => {});
    check(await tile() === 'NAR', `COMP off: the tile follows to that setting, NAR (got ${await tile()})`);
    await tapTile();
    await page.waitForFunction(() => window.__rig.tbw[0] === 0, null, { timeout: 5000 }).catch(() => {});
    check((await rig()).tbw.join() === '0,1' && await tile() === 'WIDE', `a tap steps NAR -> WIDE and writes the COMP-off setting (got ${(await rig()).tbw.join()}, ${await tile()})`);
    await tapTile();
    await page.waitForFunction(() => window.__rig.tbw[0] === 1, null, { timeout: 5000 }).catch(() => {});
    check(await tile() === 'MID', 'another tap: MID');
    // Pressed during a digital over, the operator's setting stays.
    await send({ cmd: 'setPTT', value: true, digital: true });
    await page.waitForFunction(() => window.__rig.tbw[0] === 0, null, { timeout: 5000 }).catch(() => {});
    check(await tile() === 'WIDE', `during a digital over the tile says WIDE (got ${await tile()})`);
    await tapTile();
    await page.waitForFunction(() => window.__rig.tbw[0] === 1, null, { timeout: 5000 }).catch(() => {});
    await send({ cmd: 'setPTT', value: false });
    await settle();
    check((await rig()).tbw[0] === 1 && await tile() === 'MID', `a tap during the over is not undone by its end (got ${(await rig()).tbw.join()}, ${await tile()})`);
  }

  // ---- outside SSB ----------------------------------------------------------
  {
    await send({ cmd: 'setMode', value: 'FM' });
    await page.waitForFunction(() => window.__rig.mode === 0x05 && window.currentMode === 'FM', null, { timeout: 5000 });
    const m = await mark();
    await send({ cmd: 'setPTT', value: true, digital: true });
    await settle();
    await send({ cmd: 'setPTT', value: false });
    await settle();
    const f = await since(m);
    check(f.join() === 'KEY,UNKEY', `an FM over (packet) touches neither, and the filter is not read in FM (got ${f.join(', ')})`);
    check(await tile() === null, 'no TBW tile outside SSB');
  }
  await page.close();
} finally {
  await browser.close();
  server.close();
  fs.rmSync(dist, { recursive: true, force: true });
}
console.log(fails ? `${fails} check(s) FAILED` : 'all checks passed');
process.exit(fails ? 1 : 0);

// The FUNC AGC tile in a real browser, Standalone build, never a real radio.
//
// 1. On the virtual rig (?virtual=1): FUNC has an AGC tile reading the rig's
//    setting by name; a tap steps FAST -> MID -> SLOW -> FAST like the
//    radio's AGC key and sends setAGC with the rig's value. On the rigs whose
//    AGC command has an OFF (IC-785x, IC-7200, the Kenwoods) OFF closes the
//    cycle, after the speeds.
// 2. On the Web Serial transport against a fake IC-7300 (navigator.serial
//    stubbed, the fake answers CI-V): rigInfo carries the IC-7300's list from
//    rig-caps.js, 16 12 is read at connect, a tap writes 16 12 <value> and
//    reads it back, FM keeps FAST (the rig refuses the set and the tile says
//    so), and a mode change reads AGC again (the rig keeps it per mode).
//
// 3. With WFWEB_AGC_URL=<https://...> (and only then, instead of 1 and 2):
//    the Server build's page on that wfweb, which tests/test_agc.py runs on
//    the IC-7300 mock: the tile, a tap confirmed by the server's status, and
//    a Shared Station refusal leaving the tile alone.
//
// Needs playwright-core and a Chromium (found like comp_browser.mjs);
// `--probe` only reports whether they were found. Run through
// tests/test_agc.py. WFWEB_SHOT_DIR=<dir> saves the FUNC window.

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
const dist = fs.mkdtempSync(path.join(os.tmpdir(), 'wfweb-agc-'));
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
// mode, AGC (16 12, 1 FAST / 2 MID / 3 SLOW, FAST and NG in FM like the
// radio), takes a mode set, and ACKs everything else.
function fakeIc7300() {
  const RIG = 0x94, CTL = 0xE0;
  const rig = window.__rig = { agc: 2, mode: 0x01, freq: 14074000 };
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
    if (c === 0x16 && s === 0x12) {
      const fm = rig.mode === 0x05;
      if (p.length === 2) return reply([0x16, 0x12, fm ? 1 : rig.agc]);
      if (fm || p[2] < 1 || p[2] > 3) return reply([0xFA]);
      rig.agc = p[2];
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

async function startPage(ctx, url, init) {
  const page = await ctx.newPage();
  page.on('pageerror', (e) => { console.log('pageerror: ' + e.message); fails++; });
  if (init) await page.addInitScript(init);
  await page.goto(url);
  await page.click('#startBtn');
  await page.waitForFunction(() => window.currentFreq > 0, null, { timeout: 30000 });
  await page.evaluate(() => {
    window.__cmds = [];
    const orig = window.send;
    window.send = function (obj) { window.__cmds.push(JSON.parse(JSON.stringify(obj))); return orig.apply(this, arguments); };
  });
  return page;
}

const tileState = (page) => page.evaluate(() => {
  const t = [...document.querySelectorAll('#funcGrid .ov-btn')].find(b => (b.querySelector('.btn-label') || {}).textContent === 'AGC');
  return t ? t.querySelector('.btn-state').textContent : null;
});
const tapTile = (page) => page.evaluate(() => {
  [...document.querySelectorAll('#funcGrid .ov-btn')].find(b => (b.querySelector('.btn-label') || {}).textContent === 'AGC').click();
});

try {
  const ctx = await browser.newContext({ viewport: { width: 1100, height: 800 } });

  const standalone = !process.env.WFWEB_AGC_URL;

  // ---- 1. virtual rig ------------------------------------------------------
  if (standalone) {
    const page = await startPage(ctx, BASE + '?virtual=1&id=1');
    await page.waitForFunction(() => window.hasAgc && window.agcValue !== null, null, { timeout: 10000 });
    const s = await page.evaluate(() => ({ hasAgc, agcModes, agcValue }));
    check(s.agcModes.map(m => m.name).join() === 'FAST,MID,SLOW' && s.agcValue === 2,
          `the virtual rig reports FAST/MID/SLOW, now MID (got ${JSON.stringify(s)})`);
    await page.evaluate(() => window.setPanel('func'));
    check(await tileState(page) === 'MID', `FUNC has an AGC tile reading MID (got ${await tileState(page)})`);
    const order = await page.evaluate(() => [...document.querySelectorAll('#funcGrid .ov-btn .btn-label')].map(e => e.textContent));
    check(order.indexOf('AGC') >= 0 && order.indexOf('AGC') + 1 === order.indexOf('NOTCH'),
          `the AGC tile sits just ahead of NOTCH (got ${order.join(' ')})`);
    if (process.env.WFWEB_SHOT_DIR) await page.locator('#funcOverlay').screenshot({ path: path.join(process.env.WFWEB_SHOT_DIR, 'agc-func.png') });
    const seen = [];
    for (let i = 0; i < 3; i++) {
      await tapTile(page);
      seen.push(await tileState(page));
    }
    check(seen.join() === 'SLOW,FAST,MID', `taps step MID -> SLOW -> FAST -> MID (got ${seen.join(' -> ')})`);
    const sent = (await page.evaluate(() => window.__cmds.filter(c => c.cmd === 'setAGC').map(c => c.value)));
    check(sent.join() === '3,1,2', `the taps send setAGC 3, 1, 2 (got ${sent.join()})`);

    // OFF closes the cycle on the rigs that have one there.
    const cyc = await page.evaluate(() => {
      const walk = (modes, from) => {
        updateHasAgc({ hasAgc: true, agcModes: modes });
        const out = []; let v = from;
        for (let i = 0; i < modes.length; i++) { v = nextAgc(v); out.push(agcName(v)); }
        return out.join();
      };
      return {
        ic785x: walk([{ num: 0, name: 'OFF' }, { num: 1, name: 'FAST' }, { num: 2, name: 'MID' }, { num: 3, name: 'SLOW' }], 1),
        ts890: walk([{ num: 0, name: 'OFF' }, { num: 1, name: 'SLOW' }, { num: 2, name: 'MID' }, { num: 3, name: 'FAST' }], 1),
        ic7600: walk([{ num: 0, name: 'FAST' }, { num: 1, name: 'MID' }, { num: 2, name: 'SLOW' }], 0),
        unknown: (updateHasAgc({ hasAgc: true, agcModes: [{ num: 1, name: 'FAST' }, { num: 2, name: 'MID' }] }), agcName(nextAgc(null))),
      };
    });
    check(cyc.ic785x === 'MID,SLOW,OFF,FAST', `IC-785x: FAST -> MID -> SLOW -> OFF -> FAST (got ${cyc.ic785x})`);
    check(cyc.ts890 === 'MID,FAST,OFF,SLOW', `TS-890S: SLOW -> MID -> FAST -> OFF -> SLOW (got ${cyc.ts890})`);
    check(cyc.ic7600 === 'MID,SLOW,FAST', `IC-7600 (0-2): FAST -> MID -> SLOW -> FAST (got ${cyc.ic7600})`);
    check(cyc.unknown === 'FAST', `before the rig reports, a tap goes to the first speed (got ${cyc.unknown})`);
    await page.evaluate(() => updateHasAgc({ hasAgc: false }));
    check(await tileState(page) === null, 'no AGC tile on a rig without the list');
    await page.close();
  }

  // ---- 2. Web Serial transport, fake IC-7300 --------------------------------
  if (standalone) {
    const page = await startPage(ctx, BASE, fakeIc7300);
    await page.waitForFunction(() => window.hasAgc && window.agcValue !== null, null, { timeout: 15000 });
    const s = await page.evaluate(() => ({ agcModes, agcValue }));
    check(JSON.stringify(s.agcModes) === JSON.stringify([{ num: 1, name: 'FAST' }, { num: 2, name: 'MID' }, { num: 3, name: 'SLOW' }]),
          `rigInfo carries the IC-7300's AGC list from rig-caps.js (got ${JSON.stringify(s.agcModes)})`);
    check(s.agcValue === 2, `16 12 read at connect: MID (got ${s.agcValue})`);
    const agcFrames = () => page.evaluate(() => window.__civLog.filter(p => p[0] === 0x16 && p[1] === 0x12).map(p => p.join(' ')));

    await page.evaluate(() => window.setPanel('func'));
    let mark = (await agcFrames()).length;
    await tapTile(page);
    await page.waitForFunction(() => window.__rig.agc === 3, null, { timeout: 5000 });
    await page.waitForTimeout(300);
    const frames = (await agcFrames()).slice(mark);
    const w = frames.indexOf('22 18 3');
    check(w >= 0 && frames.slice(w + 1).includes('22 18'), `a tap writes 16 12 03 and reads it back (got ${frames.join(' | ')})`);
    check(await tileState(page) === 'SLOW', 'the tile reads SLOW');

    // FM: the rig keeps FAST and refuses the set.
    await page.evaluate(() => window.send({ cmd: 'setMode', value: 'FM' }));
    await page.waitForFunction(() => window.__rig.mode === 0x05 && window.agcValue === 1, null, { timeout: 5000 });
    check(true, 'switching to FM reads AGC again: FAST');
    await tapTile(page);
    await page.waitForTimeout(600);
    check(await page.evaluate(() => window.agcValue) === 1 && await page.evaluate(() => window.__rig.agc) === 3,
          'in FM the refused set comes back as FAST on the tile');

    // Back to USB, with the AGC changed behind the page's back: re-read.
    await page.evaluate(() => { window.__rig.agc = 2; window.send({ cmd: 'setMode', value: 'USB' }); });
    await page.waitForFunction(() => window.__rig.mode === 0x01 && window.agcValue === 2, null, { timeout: 5000 });
    check(true, 'back in USB the page reads the rig\'s setting for that mode: MID');
    await page.close();
  }

  // ---- 3. the Server build's page, when tests/test_agc.py hands one over ---
  // WFWEB_AGC_URL is a wfweb on the IC-7300 mock (AGC MID): the page gets the
  // list from the server's rigInfo and its taps go through setAGC.
  if (process.env.WFWEB_AGC_URL) {
    const sctx = await browser.newContext({ viewport: { width: 1100, height: 800 }, ignoreHTTPSErrors: true });
    const page = await startPage(sctx, process.env.WFWEB_AGC_URL);
    await page.waitForFunction(() => window.hasAgc && window.agcValue === 2, null, { timeout: 15000 });
    check(await page.evaluate(() => agcModes.map(m => m.num + ':' + m.name).join()) === '1:FAST,2:MID,3:SLOW',
          'server page: rigInfo carries the IC-7300 list, AGC MID');
    await page.evaluate(() => window.setPanel('func'));
    check(await tileState(page) === 'MID', `server page: the tile reads MID (got ${await tileState(page)})`);
    await tapTile(page);
    check(await tileState(page) === 'SLOW', 'server page: a tap shows SLOW at once');
    // Let the server's read-back arrive, then put the page's value aside: only
    // a value the server pushes can bring SLOW back.
    await page.waitForTimeout(800);
    await page.evaluate(() => { agcValue = null; });
    await page.evaluate(() => window.send({ cmd: 'getStatus' }));
    await page.waitForFunction(() => window.agcValue === 3, null, { timeout: 8000 })
      .then(() => check(true, 'server page: the status confirms the rig is on SLOW'),
            () => check(false, 'server page: the status never said SLOW'));
    // A Shared Station refusal: nothing goes out, the tile keeps its setting.
    const refused = await page.evaluate(() => {
      const saved = { shared: accessState.shared, denied: accessState.denied };
      accessState.shared = true; accessState.denied = { setAGC: 'operate' };
      const before = agcValue;
      [...document.querySelectorAll('#funcGrid .ov-btn')].find(b => b.querySelector('.btn-label').textContent === 'AGC').click();
      const after = agcValue;
      accessState.shared = saved.shared; accessState.denied = saved.denied;
      return { before, after };
    });
    check(refused.before === 3 && refused.after === 3, `server page: a refused tap leaves the tile alone (got ${JSON.stringify(refused)})`);
    await page.evaluate(() => window.send({ cmd: 'setAGC', value: 2 }));
    await page.waitForFunction(() => window.agcValue === 2, null, { timeout: 8000 });
    await sctx.close();
  }
} finally {
  await browser.close();
  server.close();
  fs.rmSync(dist, { recursive: true, force: true });
}
console.log(fails ? `${fails} check(s) FAILED` : 'all checks passed');
process.exit(fails ? 1 : 0);

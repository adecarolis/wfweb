// The tuner controls in a real browser, Standalone build, never a real radio
// (#156).
//
// 1. On the virtual rig (?virtual=1): the top-bar TUNE tag is there with the
//    tuner OFF (grey), red when ON, blinking while it tunes. A click turns the
//    tuner on or off, a hold starts a tune from OFF too, a press dragged off
//    the tag does nothing. FUNC > TUNER does the same (click, hold, Enter, a
//    release on a tile the grid rebuilt mid-press), both carry the hint in
//    their tooltip, and a touch hold on a touch screen tunes. A rig without a
//    tuner has neither.
// 2. On the Web Serial transport against a fake IC-7300 that keeps the tuner
//    per band like the radio ([TUNER] Switch = Auto): the tag follows a band
//    change, and a tune's blink ends when the rig says the tune is over.
//
// Needs playwright-core and a Chromium (found like agc_browser.mjs); `--probe`
// only reports whether they were found. Run through tests/test_tuner.py.
// WFWEB_SHOT_DIR=<dir> saves the top bar in each state.

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
const dist = fs.mkdtempSync(path.join(os.tmpdir(), 'wfweb-tuner-'));
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

// A fake IC-7300 behind navigator.serial that keeps the tuner ON or OFF per
// band, as the radio does with [TUNER] Switch = Auto: OFF on 20 m, ON on
// 15 m. 1C 01 02 tunes for 1.5 s and leaves the tuner ON for that band.
function fakeIc7300() {
  const RIG = 0x94, CTL = 0xE0;
  const rig = window.__rig = { mode: 0x01, freq: 14074000, tuner: { 14: 0, 21: 1 }, tuning: false };
  const band = () => Math.floor(rig.freq / 1e6);
  const log = window.__civLog = [];
  let ctrl = null;
  const readable = new ReadableStream({ start(c) { ctrl = c; } });
  const reply = (bytes) => ctrl.enqueue(new Uint8Array([0xFE, 0xFE, CTL, RIG, ...bytes, 0xFD]));
  const bcd = (hz) => { const out = []; for (let i = 0; i < 5; i++) { const two = hz % 100; hz = Math.floor(hz / 100); out.push(((two / 10 | 0) << 4) | (two % 10)); } return out; };
  const unbcd = (b) => { let hz = 0, m = 1; for (const x of b) { hz += ((x & 0x0F) + (x >> 4) * 10) * m; m *= 100; } return hz; };
  const respond = (p) => {
    const [c, s] = p;
    if (c === 0x19) return reply([0x19, 0x00, RIG]);
    if (c === 0x03) return reply([0x03, ...bcd(rig.freq)]);
    if (c === 0x04) return reply([0x04, rig.mode, 0x01]);
    if (c === 0x05 && p.length >= 6) { rig.freq = unbcd(p.slice(1, 6)); return reply([0xFB]); }
    if (c === 0x06 && p.length >= 2) { rig.mode = p[1]; return reply([0xFB]); }
    if (c === 0x1C && s === 0x01) {
      if (p.length === 2) return reply([0x1C, 0x01, rig.tuning ? 2 : (rig.tuner[band()] || 0)]);
      if (p[2] === 2) {
        const b = band();
        rig.tuning = true;
        setTimeout(() => { rig.tuning = false; rig.tuner[b] = 1; }, 1500);
      } else {
        rig.tuner[band()] = p[2] ? 1 : 0;
      }
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
  await page.waitForFunction(() => window.currentFreq > 0 && window.hasTuner, null, { timeout: 30000 });
  await page.evaluate(() => {
    window.__cmds = [];
    const orig = window.send;
    window.send = function (obj) { window.__cmds.push(JSON.parse(JSON.stringify(obj))); return orig.apply(this, arguments); };
  });
  return page;
}

// What the tag shows: hidden / off / on / tuning, and its colour.
const tag = (page) => page.evaluate(() => {
  const el = document.getElementById('tuneLabel');
  const cs = getComputedStyle(el);
  const shown = cs.display !== 'none' && el.offsetWidth > 0;
  return {
    state: !shown ? 'hidden' : el.classList.contains('tuning') ? 'tuning' : el.classList.contains('active') ? 'on' : 'off',
    color: cs.color, title: el.title,
  };
});
const tunerCmds = (page) => page.evaluate(() => {
  const out = window.__cmds.filter(c => c.cmd === 'setTuner').map(c => c.value);
  window.__cmds = [];
  return out.join();
});
const tile = (page) => page.evaluate(() => {
  const t = [...document.querySelectorAll('#funcGrid .ov-btn')].find(b => (b.querySelector('.btn-label') || {}).textContent === 'TUNER');
  return t ? { state: t.querySelector('.btn-state').textContent, title: t.title } : null;
});
const tileLoc = (page) => page.locator('#funcGrid .ov-btn', { hasText: 'TUNER' });

async function press(page, locator, ms) {
  const b = await locator.boundingBox();
  await page.mouse.move(b.x + b.width / 2, b.y + b.height / 2);
  await page.mouse.down();
  await page.waitForTimeout(ms);
  await page.mouse.up();
  await page.waitForTimeout(150);
}
const shot = async (page, name) => {
  if (process.env.WFWEB_SHOT_DIR) await page.locator('#topBar').screenshot({ path: path.join(process.env.WFWEB_SHOT_DIR, name) });
};

try {
  // ---- 1. virtual rig ------------------------------------------------------
  {
    const ctx = await browser.newContext({ viewport: { width: 1100, height: 800 } });
    const page = await startPage(ctx, BASE + '?virtual=1&id=1');
    const tl = page.locator('#tuneLabel');
    await page.evaluate(() => updateTunerState(0));

    let t = await tag(page);
    check(t.state === 'off' && t.color === 'rgb(136, 136, 136)', `tuner OFF: the TUNE tag is there, grey (got ${JSON.stringify(t)})`);
    check(/Hold: tune/.test(t.title), `the tag's tooltip says a hold tunes (got "${t.title}")`);
    await shot(page, 'tuner-off.png');

    await press(page, tl, 80);
    check(await tunerCmds(page) === '1', 'a click on the grey tag turns the tuner on');
    t = await tag(page);
    check(t.state === 'on' && t.color === 'rgb(204, 0, 0)', `tuner ON: the tag is red (got ${JSON.stringify(t)})`);
    await shot(page, 'tuner-on.png');

    await press(page, tl, 80);
    check(await tunerCmds(page) === '0', 'a click on the red tag turns the tuner off');
    check((await tag(page)).state === 'off', 'and the tag stays, grey');

    await press(page, tl, 700);
    check(await tunerCmds(page) === '2', 'a hold from OFF starts a tune, and its release sends nothing more');
    check((await tag(page)).state === 'tuning', 'the tag blinks while it tunes');
    await shot(page, 'tuner-tuning.png');
    await page.waitForFunction(() => window.tunerState === 1, null, { timeout: 5000 });
    check((await tag(page)).state === 'on', 'the tune over, the tag is red');

    // Pressed, then dragged off before the hold time: nothing.
    {
      const b = await tl.boundingBox();
      await page.mouse.move(b.x + b.width / 2, b.y + b.height / 2);
      await page.mouse.down();
      await page.mouse.move(b.x + b.width / 2, b.y + 200);
      await page.waitForTimeout(700);
      await page.mouse.up();
      await page.waitForTimeout(100);
      check(await tunerCmds(page) === '', 'a press dragged off the tag sends nothing');
    }

    // FUNC > TUNER: the same click and hold.
    await page.evaluate(() => window.setPanel('func'));
    let ti = await tile(page);
    check(ti && ti.state === 'ON' && /Hold: tune/.test(ti.title), `FUNC has the TUNER tile, ON, with the hint (got ${JSON.stringify(ti)})`);
    await press(page, tileLoc(page), 80);
    check(await tunerCmds(page) === '0' && (await tile(page)).state === 'OFF', 'a click on the tile turns the tuner off');
    await press(page, tileLoc(page), 700);
    check(await tunerCmds(page) === '2', 'a hold on the tile starts a tune');
    await page.waitForFunction(() => window.tunerState === 1, null, { timeout: 5000 });
    check((await tile(page)).state === 'ON', 'the tune over, the tile reads ON');

    // The grid is rebuilt mid-press: the release on the new tile is still a click.
    {
      const b = await tileLoc(page).boundingBox();
      await page.mouse.move(b.x + b.width / 2, b.y + b.height / 2);
      await page.mouse.down();
      await page.evaluate(() => renderFuncOverlay());
      await page.waitForTimeout(100);
      await page.mouse.up();
      await page.waitForTimeout(700);
      check(await tunerCmds(page) === '0', 'a release on a tile rebuilt mid-press is one click, no tune');
    }

    await tileLoc(page).focus();
    await page.keyboard.press('Enter');
    check(await tunerCmds(page) === '1', 'Enter on the focused tile turns the tuner on');
    await page.evaluate(() => window.setPanel('default'));

    await page.evaluate(() => updateHasTuner(false));
    check((await tag(page)).state === 'hidden', 'a rig without a tuner has no TUNE tag');
    await page.evaluate(() => window.setPanel('func'));
    check(await tile(page) === null, 'and no TUNER tile');
    await ctx.close();
  }

  // Touch screen: a tap toggles, a held finger tunes.
  {
    const ctx = await browser.newContext({ viewport: { width: 900, height: 700 }, hasTouch: true });
    const page = await startPage(ctx, BASE + '?virtual=1&id=2');
    await page.evaluate(() => updateTunerState(0));
    const b = await page.locator('#tuneLabel').boundingBox();
    const x = b.x + b.width / 2, y = b.y + b.height / 2;
    const cdp = await ctx.newCDPSession(page);
    const touch = async (ms) => {
      await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x, y }] });
      await page.waitForTimeout(ms);
      await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
      await page.waitForTimeout(150);
    };
    await touch(60);
    check(await tunerCmds(page) === '1', 'touch: a tap on the grey tag turns the tuner on');
    await touch(60);
    check(await tunerCmds(page) === '0', 'touch: a tap on the red tag turns it off');
    await touch(800);
    check(await tunerCmds(page) === '2', 'touch: a held finger on the grey tag starts a tune');
    await ctx.close();
  }

  // ---- 2. Web Serial transport, fake IC-7300 with the tuner per band ------
  {
    const ctx = await browser.newContext({ viewport: { width: 1100, height: 800 } });
    const page = await startPage(ctx, BASE, fakeIc7300);
    await page.waitForFunction(() => window.tunerState === 0, null, { timeout: 8000 });
    check((await tag(page)).state === 'off', '20 m (tuner OFF on this band): the tag is grey');

    await page.evaluate(() => window.send({ cmd: 'setFrequency', value: 21074000 }));
    const t0 = Date.now();
    await page.waitForFunction(() => window.tunerState === 1, null, { timeout: 4000 })
      .then(() => check(true, `to 15 m (tuner ON there): the tag turns red after ${Date.now() - t0} ms`),
            () => check(false, 'to 15 m: the tag never turned red'));

    await page.evaluate(() => window.send({ cmd: 'setFrequency', value: 14074000 }));
    await page.waitForFunction(() => window.tunerState === 0, null, { timeout: 4000 })
      .then(() => check(true, 'back to 20 m: grey again'), () => check(false, 'back to 20 m: the tag stayed red'));

    await press(page, page.locator('#tuneLabel'), 700);
    const frames = await page.evaluate(() => window.__civLog.filter(p => p[0] === 0x1C && p[1] === 0x01 && p.length === 3).map(p => p[2]));
    check(frames.join() === '2', `a hold writes 1C 01 02 (got ${frames.join()})`);
    check((await tag(page)).state === 'tuning', 'the tag blinks');
    await page.waitForFunction(() => window.tunerState === 1, null, { timeout: 5000 })
      .then(() => check(true, 'the rig ends the tune: the page reads it and the tag is red'),
            () => check(false, 'the tag kept blinking after the rig ended the tune'));
    check(await page.evaluate(() => window.__rig.tuner[14]) === 1, 'and the rig now keeps the tuner ON on 20 m');
    await ctx.close();
  }
} finally {
  await browser.close();
  server.close();
  fs.rmSync(dist, { recursive: true, force: true });
}
console.log(fails ? `${fails} check(s) FAILED` : 'all checks passed');
process.exit(fails ? 1 : 0);

// Speech compressor controls and the digital-TX hold (#147), in a real
// browser: the Standalone SPA on ?virtual=1, never a real radio.
//
// - FUNC has a COMP tile: it carries the level (0-10) while on, OFF while
//   off, and switches the rig's compressor.
// - GAIN has a COMP row: the level, shown 0-10 like the radio's screen.
// - TUNE keys the rig as a digital over ({setPTT, digital: true}), and the
//   transport takes COMP out for that over and puts it back after it; a plain
//   PTT leaves it alone.
// - The spectrum carries a grey COMP n tile next to SQL while COMP is on.
//
// Needs playwright-core and a Chromium (found like midi_browser.mjs);
// `--probe` only reports whether they were found. Run through
// tests/test_compressor.py. WFWEB_SHOT_DIR=<dir> saves the two windows.

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
const dist = fs.mkdtempSync(path.join(os.tmpdir(), 'wfweb-comp-'));
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

const browser = await chromium.launch({
  executablePath: found.chrome, headless: true,
  args: ['--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream', '--autoplay-policy=no-user-gesture-required'],
});
try {
  const ctx = await browser.newContext({ viewport: { width: 1100, height: 800 } });
  const page = await ctx.newPage();
  page.on('pageerror', (e) => { console.log('pageerror: ' + e.message); fails++; });
  await page.goto(URL0);
  await page.click('#startBtn');
  await page.waitForFunction(() => window.currentFreq > 0, null, { timeout: 30000 });
  await page.evaluate(() => {
    window.__cmds = [];
    const orig = window.send;
    window.send = function (obj) { window.__cmds.push(JSON.parse(JSON.stringify(obj))); return orig.apply(this, arguments); };
  });
  const cmds = (name) => page.evaluate((n) => window.__cmds.filter(c => c.cmd === n), name);
  const state = () => page.evaluate(() => ({ compOn, compLevel, hasCompressor, hasCompLevel, mode: currentMode }));
  await page.waitForFunction(() => window.hasCompressor && window.compLevel !== null, null, { timeout: 10000 });
  let s = await state();
  check(s.hasCompressor && s.hasCompLevel && s.compOn === true && s.compLevel === 128,
        `the rig reports COMP on at level 128 (got ${JSON.stringify(s)})`);
  const ind = () => page.evaluate(() => { const e = document.getElementById('compIndicator');
    return e && e.style.display !== 'none' ? e.textContent : null; });
  check((await ind()) === 'COMP 5', `the spectrum shows a COMP 5 tile (got ${await ind()})`);

  // -- FUNC: the COMP tile --
  await page.evaluate(() => window.setPanel('func'));
  const tile = page.locator('#funcGrid .ov-btn', { hasText: 'COMP' });
  check(await tile.count() === 1, 'FUNC grid has a COMP tile');
  check((await tile.locator('.btn-state').textContent()) === '5' && /\bactive\b/.test(await tile.getAttribute('class')),
        'COMP tile is lit and carries the level, 5 of 10');
  if (process.env.WFWEB_SHOT_DIR) await page.locator('#funcOverlay').screenshot({ path: path.join(process.env.WFWEB_SHOT_DIR, 'comp-func.png') });
  await tile.click();
  await page.waitForFunction(() => window.compOn === false);
  const tileOff = page.locator('#funcGrid .ov-btn', { hasText: 'COMP' });
  check((await tileOff.locator('.btn-state').textContent()) === 'OFF', 'a tap switches COMP off and the tile reads OFF');
  check((await ind()) === null, 'the spectrum COMP tile hides while COMP is off');
  check((await cmds('setCompressor')).some(c => c.value === false), 'the tap sends setCompressor false');
  await tileOff.click();
  await page.waitForFunction(() => window.compOn === true);
  check(true, 'a second tap switches COMP back on');

  // -- GAIN: the COMP row --
  await page.evaluate(() => window.setPanel('gain'));
  check(await page.locator('#compLevelRow').isVisible(), 'GAIN window shows the COMP row');
  check((await page.locator('#compLevelVal').textContent()) === '5', 'the COMP row reads 5');
  await page.evaluate(() => {
    const sl = document.getElementById('compLevelSlider');
    sl.value = '204';
    sl.dispatchEvent(new Event('input', { bubbles: true }));
  });
  await page.waitForFunction(() => window.__cmds.some(c => c.cmd === 'setCompLevel' && c.value === 204), null, { timeout: 3000 });
  check((await page.locator('#compLevelVal').textContent()) === '8', 'moving the slider sends setCompLevel 204 and reads 8');
  check((await ind()) === 'COMP 8', 'the spectrum tile follows the level (COMP 8)');
  if (process.env.WFWEB_SHOT_DIR) await page.locator('#gainOverlay').screenshot({ path: path.join(process.env.WFWEB_SHOT_DIR, 'comp-gain.png') });
  await page.evaluate(() => window.setPanel('default'));

  // -- digital over: TUNE flags its key, the transport holds COMP off --
  if ((await state()).mode !== 'USB') {
    await page.evaluate(() => window.setMode('USB'));
    await page.waitForFunction(() => window.currentMode === 'USB');
  }
  await page.evaluate(() => { window.__compSeen = []; const prev = window.updateCompressor;
    window.updateCompressor = function (on) { window.__compSeen.push(!!on); return prev.apply(this, arguments); }; });
  // TUNE needs the DIGI TX AudioContext the FT8 panel creates on open.
  await page.evaluate(() => window.initDigiTxCtx());
  await page.waitForFunction(() => window.digiTxCtx && window.digiTxCtx.state === 'running', null, { timeout: 5000 });
  await page.evaluate(() => window.startDigiTune());
  await page.waitForFunction(() => window.__cmds.some(c => c.cmd === 'setPTT' && c.value === true), null, { timeout: 10000 });
  const keys = (await cmds('setPTT')).filter(c => c.value === true);
  check(keys.length > 0 && keys.every(c => c.digital === true), 'TUNE keys the rig as a digital over');
  await page.waitForFunction(() => window.compOn === false, null, { timeout: 3000 });
  check((await ind()) === null, 'COMP goes off for the over, and its spectrum tile with it');
  await page.evaluate(() => window.stopDigiTune());
  await page.waitForFunction(() => window.compOn === true, null, { timeout: 3000 });
  check((await ind()) === 'COMP 8', 'COMP is back on after the unkey, tile included');
  if (process.env.WFWEB_SHOT_DIR) {
    await page.evaluate(() => window.send({ cmd: 'setSquelch', value: 64 }));
    await page.waitForFunction(() => { const e = document.getElementById('squelchIndicator'); return e && e.style.display !== 'none'; });
    await page.screenshot({ path: path.join(process.env.WFWEB_SHOT_DIR, 'comp-indicator.png'), clip: { x: 0, y: 0, width: 1100, height: 520 } });
    await page.evaluate(() => window.send({ cmd: 'setSquelch', value: 0 }));
  }

  // -- a voice over leaves COMP alone --
  await page.evaluate(() => { window.__compSeen = []; window.send({ cmd: 'setPTT', value: true }); });
  await page.waitForTimeout(400);
  await page.evaluate(() => window.send({ cmd: 'setPTT', value: false }));
  await page.waitForTimeout(400);
  check(await page.evaluate(() => window.compOn === true && !window.__compSeen.includes(false)), 'a plain PTT leaves COMP on');
} finally {
  await browser.close();
  server.close();
  fs.rmSync(dist, { recursive: true, force: true });
}
console.log(fails ? `${fails} check(s) FAILED` : 'all checks passed');
process.exit(fails ? 1 : 0);

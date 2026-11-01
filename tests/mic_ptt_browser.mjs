// The iOS microphone follows the voice PTT (#151), in a real browser: the
// Standalone SPA on ?virtual=1, never a real radio.
//
// With the mic open, WebKit on iOS keeps the audio session in play-and-record
// and plays the receiver several times quieter. So on iOS MIC ON only arms
// the mic: the capture opens on a voice key and closes on the unkey, and the
// session is ambient between overs. Checked here with an iPhone user agent
// and a stand-in navigator.audioSession (Chromium has none):
// - MIC ON leaves no capture open, the session ambient, no TX frames;
// - a voice key opens it (play-and-record, frames flow), the unkey closes it;
// - a digital key (FT8 & co.) does not open it;
// - a key released before getUserMedia answers leaves nothing open;
// - MIC OFF while keyed closes it.
// And with a desktop user agent the mic stays open while it is ON, as before.
//
// Needs playwright-core and a Chromium (found like comp_browser.mjs);
// `--probe` only reports whether they were found. Run through
// tests/test_mic_ptt.py.

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
const dist = fs.mkdtempSync(path.join(os.tmpdir(), 'wfweb-micptt-'));
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

const IPHONE_UA = 'Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Mobile/15E148 Safari/604.1';

// Every capture getUserMedia hands out, the session types the page sets, and
// the TX frames it sends.
function instrument() {
  window.__streams = [];
  window.__sessionTypes = [];
  let type = 'auto';
  Object.defineProperty(navigator, 'audioSession', { configurable: true, value: {
    get type() { return type; },
    set type(t) { type = t; window.__sessionTypes.push(t); },
  } });
  const gum = navigator.mediaDevices.getUserMedia.bind(navigator.mediaDevices);
  navigator.mediaDevices.getUserMedia = function (c) {
    return gum(c).then(s => { window.__streams.push(s); return s; });
  };
}

async function openPage(ctx) {
  await ctx.addInitScript(instrument);
  const page = await ctx.newPage();
  page.on('pageerror', (e) => { console.log('pageerror: ' + e.message); fails++; });
  await page.goto(URL0);
  await page.click('#startBtn');
  await page.waitForFunction(() => window.currentFreq > 0, null, { timeout: 30000 });
  await page.evaluate(() => {
    window.__frames = 0;
    const orig = window.sendTxAudio;
    window.sendTxAudio = function () { window.__frames++; return orig.apply(this, arguments); };
  });
  return page;
}

const st = (page) => page.evaluate(() => ({
  follows: micFollowsPtt, enabled: micEnabled, open: !!micStream,
  live: window.__streams.filter(s => s.getTracks().some(t => t.readyState === 'live')).length,
  session: navigator.audioSession.type, frames: window.__frames,
}));
const framesOver = async (page, ms) => {
  const a = await page.evaluate(() => window.__frames);
  await page.waitForTimeout(ms);
  return (await page.evaluate(() => window.__frames)) - a;
};

const browser = await chromium.launch({
  executablePath: found.chrome, headless: true,
  args: ['--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream', '--autoplay-policy=no-user-gesture-required'],
});
try {
  // -- iOS: the capture follows the voice PTT --
  const ios = await browser.newContext({ viewport: { width: 1100, height: 800 }, userAgent: IPHONE_UA });
  let page = await openPage(ios);
  await page.evaluate(() => window.startMic());
  await page.waitForFunction(() => window.micEnabled, null, { timeout: 10000 });
  await page.waitForTimeout(300);
  let s = await st(page);
  check(s.follows, 'an iPhone user agent turns micFollowsPtt on');
  check(s.enabled && !s.open && s.live === 0, `MIC ON on iOS arms the mic and leaves no capture open (got ${JSON.stringify(s)})`);
  check(s.session === 'ambient', `the session is ambient between overs (got ${s.session})`);
  check((await framesOver(page, 500)) === 0, 'no TX frames while receiving');

  await page.evaluate(() => window.send({ cmd: 'setPTT', value: true }));
  await page.waitForFunction(() => !!window.micStream, null, { timeout: 5000 });
  s = await st(page);
  check(s.session === 'play-and-record' && s.live === 1, `a voice key opens one capture in play-and-record (got ${JSON.stringify(s)})`);
  check((await framesOver(page, 500)) > 0, 'TX frames flow while keyed');
  await page.evaluate(() => window.send({ cmd: 'setPTT', value: false }));
  await page.waitForTimeout(200);
  s = await st(page);
  check(s.enabled && !s.open && s.live === 0 && s.session === 'ambient', `the unkey closes it, MIC stays ON (got ${JSON.stringify(s)})`);
  check((await framesOver(page, 500)) === 0, 'TX frames stop after the unkey');

  await page.evaluate(() => window.send({ cmd: 'setPTT', value: true, digital: true }));
  await page.waitForTimeout(500);
  s = await st(page);
  check(!s.open && s.live === 0 && s.session === 'ambient', `a digital key does not open the mic (got ${JSON.stringify(s)})`);
  await page.evaluate(() => window.send({ cmd: 'setPTT', value: false }));

  await page.evaluate(() => { window.send({ cmd: 'setPTT', value: true }); window.send({ cmd: 'setPTT', value: false }); });
  await page.waitForTimeout(800);
  s = await st(page);
  check(!s.open && s.live === 0 && s.session === 'ambient', `a key released before the mic opened leaves nothing open (got ${JSON.stringify(s)})`);

  await page.evaluate(() => window.send({ cmd: 'setPTT', value: true }));
  await page.waitForFunction(() => !!window.micStream, null, { timeout: 5000 });
  await page.evaluate(() => window.stopMic());
  await page.waitForTimeout(200);
  s = await st(page);
  check(!s.enabled && !s.open && s.live === 0 && s.session === 'ambient', `MIC OFF while keyed closes the capture (got ${JSON.stringify(s)})`);
  await page.evaluate(() => window.send({ cmd: 'setPTT', value: false }));
  await ios.close();

  // -- desktop: the mic stays open while it is ON --
  const desk = await browser.newContext({ viewport: { width: 1100, height: 800 } });
  page = await openPage(desk);
  await page.evaluate(() => window.startMic());
  await page.waitForFunction(() => window.micEnabled, null, { timeout: 10000 });
  await page.waitForTimeout(300);
  s = await st(page);
  check(!s.follows && s.open && s.live === 1, `on the desktop MIC ON keeps the capture open (got ${JSON.stringify(s)})`);
  await page.evaluate(() => { window.send({ cmd: 'setPTT', value: true }); window.send({ cmd: 'setPTT', value: false }); });
  await page.waitForTimeout(300);
  s = await st(page);
  check(s.open && s.live === 1, 'and a key / unkey leaves it open');
  await desk.close();
} finally {
  await browser.close();
  server.close();
  fs.rmSync(dist, { recursive: true, force: true });
}
console.log(fails ? `${fails} check(s) FAILED` : 'all checks passed');
process.exit(fails ? 1 : 0);

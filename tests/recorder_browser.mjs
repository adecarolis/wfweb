// What the recorder takes from the mic, in a real browser: the Standalone SPA
// on ?virtual=1, never a real radio.
//
// The mic streams for as long as it is enabled, keyed or not; the recorder
// keeps it only while it is what goes on the air:
// - CW: never the mic (the rig keys its keyer), but the TONE sidetone.
// - USB voice: the mic, while keyed.
// - a modem over (RTTY's open channel, which sends no digiTxActive, so the
//   page keeps sending mic frames and the transport drops them): the modem
//   audio only.
// The mic is faked as a 1500 Hz tone, so 1500 Hz in a recording is the mic;
// each recording is decoded and measured per 100 ms window.
//
// Needs playwright-core and a Chromium (found like midi_browser.mjs);
// `--probe` only reports whether they were found. Run through
// tests/test_recorder.py.

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
const dist = fs.mkdtempSync(path.join(os.tmpdir(), 'wfweb-rec-'));
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

const MIC_HZ = 1500, MODEM_HZ = 1000;
const browser = await chromium.launch({
  executablePath: found.chrome, headless: true,
  args: ['--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream', '--autoplay-policy=no-user-gesture-required'],
});
try {
  const ctx = await browser.newContext({ viewport: { width: 1100, height: 800 }, acceptDownloads: false });
  await ctx.addInitScript((hz) => {
    navigator.mediaDevices.getUserMedia = async () => {
      const ac = new AudioContext({ sampleRate: 48000 });
      const osc = ac.createOscillator(), g = ac.createGain(), d = ac.createMediaStreamDestination();
      osc.frequency.value = hz; g.gain.value = 0.5;
      osc.connect(g); g.connect(d); osc.start(); ac.resume();
      window.__fakeMic = ac;
      return d.stream;
    };
  }, MIC_HZ);
  const page = await ctx.newPage();
  page.on('pageerror', (e) => { console.log('pageerror: ' + e.message); fails++; });
  await page.goto(URL0);
  await page.click('#startBtn');
  await page.waitForFunction(() => window.currentFreq > 0, null, { timeout: 30000 });
  await page.evaluate(() => window.startMic());
  await page.waitForFunction(() => window.micEnabled === true, null, { timeout: 10000 });

  async function setMode(m) {
    await page.evaluate((v) => window.setMode(v), m);
    await page.waitForFunction((v) => window.currentMode === v, m, { timeout: 5000 });
  }
  // Records while body runs; returns the largest amplitude at each frequency
  // over 100 ms windows of the decoded file.
  async function record(body, freqs) {
    await page.evaluate(() => window.Recorder.start('audio'));
    await page.waitForFunction(() => window.Recorder.isRecording(), null, { timeout: 5000 });
    await body();
    return page.evaluate(async (fs) => {
      const r = await window.Recorder.stop();
      const ab = await new OfflineAudioContext(1, 48000, 48000).decodeAudioData(await r.blob.arrayBuffer());
      const x = ab.getChannelData(0), rate = ab.sampleRate, W = Math.round(rate / 10);
      const out = {};
      for (const f of fs) {
        let best = 0;
        const w = 2 * Math.PI * f / rate;
        for (let s = 0; s + W <= x.length; s += W) {
          let re = 0, im = 0;
          for (let i = 0; i < W; i++) { re += x[s + i] * Math.cos(w * i); im += x[s + i] * Math.sin(w * i); }
          best = Math.max(best, 2 * Math.hypot(re, im) / W);
        }
        out[f] = +best.toFixed(4);
      }
      out.seconds = +(x.length / rate).toFixed(1);
      return out;
    }, freqs);
  }
  const keyed = (on, digital) => page.evaluate(([v, d]) => window.send(d ? { cmd: 'setPTT', value: v, digital: true } : { cmd: 'setPTT', value: v }), [on, !!digital]);
  const PRESENT = 0.05, ABSENT = 0.01;

  // -- CW: the rig keyed with the mic open; the sidetone, not the mic --
  await setMode('CW');
  const toneHz = await page.evaluate(() => { window.cwSidetone.setEnabled(true); return window.cwPitchHz; });
  let a = await record(async () => {
    await keyed(true);
    await page.waitForFunction(() => window.isTransmitting === true, null, { timeout: 5000 });
    await page.evaluate(() => window.sendCWText('TEST TEST'));
    await page.waitForTimeout(3500);
    await keyed(false);
    await page.waitForTimeout(500);
  }, [MIC_HZ, toneHz]);
  console.log(`CW recording: ${JSON.stringify(a)} (tone ${toneHz} Hz)`);
  check(a[MIC_HZ] < ABSENT, `CW: the mic is not recorded while keyed (${a[MIC_HZ]})`);
  check(a[toneHz] > PRESENT, `CW: the TONE sidetone is recorded (${a[toneHz]} at ${toneHz} Hz)`);

  // -- USB voice: the mic is the signal --
  await setMode('USB');
  a = await record(async () => {
    await keyed(true);
    await page.waitForFunction(() => window.isTransmitting === true, null, { timeout: 5000 });
    await page.waitForTimeout(1500);
    await keyed(false);
    await page.waitForTimeout(500);
  }, [MIC_HZ]);
  console.log(`voice recording: ${JSON.stringify(a)}`);
  check(a[MIC_HZ] > PRESENT, `USB voice: the mic is recorded while keyed (${a[MIC_HZ]})`);

  // -- a modem over as the RTTY bar sends it: open channel, PTT after the
  //    first chunks, unkey on done; the mic keeps streaming throughout --
  await page.evaluate(() => window.initDigiTxCtx());
  await page.waitForFunction(() => window.digiTxCtx && window.digiTxCtx.state === 'running', null, { timeout: 5000 });
  a = await record(async () => {
    await page.evaluate((hz) => new Promise((resolve) => {
      const s = window.openDigiStream(null,
        () => { window.send({ cmd: 'setPTT', value: false }); resolve(); },
        () => window.send({ cmd: 'setPTT', value: true, digital: true }));
      const n = 12000 * 2, buf = new Float32Array(n);
      for (let i = 0; i < n; i++) buf[i] = 0.8 * Math.sin(2 * Math.PI * hz * i / 12000);
      s.push(buf); s.end();
    }), MODEM_HZ);
    await page.waitForFunction(() => window.isTransmitting === false, null, { timeout: 5000 });
    await page.waitForTimeout(500);
  }, [MIC_HZ, MODEM_HZ]);
  console.log(`modem recording: ${JSON.stringify(a)}`);
  check(a[MODEM_HZ] > PRESENT, `modem over: the modem audio is recorded (${a[MODEM_HZ]})`);
  check(a[MIC_HZ] < ABSENT, `modem over: the mic is not mixed in (${a[MIC_HZ]})`);
} finally {
  await browser.close();
  server.close();
  fs.rmSync(dist, { recursive: true, force: true });
}
console.log(fails ? `${fails} check(s) FAILED` : 'all checks passed');
process.exit(fails ? 1 : 0);

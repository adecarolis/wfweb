// End-to-end CW keyer bench on the two-rig virtual testrig (#141).
//
// Run by tests/test_cw_keyer.py with the bench up (./scripts/testrig.sh up 2):
// rig A keys from its CW bar, rig B hears A's keyed audio.  Checks, against
// the radio's actual keying rather than the page's own model:
//   - the model stays in step with the rig over a long multi-word text
//     (B taps its RX frames before the jitter buffer; the frames' sample
//     counts give a timeline immune to arrival jitter),
//   - CHR-mode type-ahead far faster than the keyer never overflows the
//     rig's keyer buffer (the virtual rig NGs a frame that would, like the
//     real one) and B copies every character,
//   - the bench itself catches an overflow when the pacing is bypassed,
//   - Backspace cuts the rig and the next character starts at once,
//   - the browser sidetone plays every element across spaces and pauses, in
//     step with the model, and STOP cuts it.
// Needs playwright-core and a Chromium; `--probe` exits 0 when both are found
// and 3 otherwise, so the pytest wrapper can skip.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(here, '..');
const URL_A = process.env.WFWEB_BENCH_A || 'https://127.0.0.1:9080/';
const URL_B = process.env.WFWEB_BENCH_B || 'https://127.0.0.1:9090/';
const VR_LOG = path.join(ROOT, '.testrig', 'virtualrig.log');

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
      for (const r of revs) for (const sub of ['chrome-linux64/chrome', 'chrome-linux/chrome']) {
        const p = path.join(base, r, sub);
        if (fs.existsSync(p)) { chrome = p; break; }
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

const sleep = ms => new Promise(r => setTimeout(r, ms));
let fails = 0;
function check(ok, msg) { console.log((ok ? 'PASS ' : 'FAIL ') + msg); if (!ok) fails++; }
const avg = a => a.reduce((x, y) => x + y, 0) / a.length;

async function bringUp(page, url) {
  await page.goto(url);
  await page.evaluate(() => { localStorage.clear(); sessionStorage.clear(); });
  await page.goto(url);
  await page.click('#startBtn');
  await page.waitForFunction(() => window.currentFreq > 0, null, { timeout: 20000 });
  await page.waitForFunction(() => window.audioEnabled === true, null, { timeout: 20000 });
  await page.evaluate(() => window.setMode('CW'));
  await page.waitForFunction(() => window.currentMode === 'CW', null, { timeout: 10000 });
  await page.evaluate(() => window.renderCWOverlay());
  await page.waitForFunction(() => !document.getElementById('cwBar').classList.contains('hidden'));
}
// B: RX PCM as it arrives (0x02 frames), per-frame RMS + sample count.
async function tapRx(page) {
  await page.evaluate(() => {
    window.__rx = [];
    const orig = window.handleAudioData;
    window.handleAudioData = function (buffer) {
      try {
        const pcm = new Int16Array(buffer, 6);
        let s = 0; for (let i = 0; i < pcm.length; i++) s += pcm[i] * pcm[i];
        window.__rx.push({ t: Date.now(), v: Math.sqrt(s / pcm.length) / 32768, n: pcm.length });
      } catch (e) {}
      return orig.apply(this, arguments);
    };
  });
}
// A: sidetone level through an analyser on the keyer's master gain.
async function tapTone(page) {
  await page.evaluate(() => {
    window.__tone = [];
    window.__tapStart = function () {
      const st = window.cwSidetone;
      if (!st || !st.ctx || window.__an) return;
      const an = st.ctx.createAnalyser(); an.fftSize = 256; an.smoothingTimeConstant = 0;
      st.master.connect(an); window.__an = an;
      const buf = new Float32Array(an.fftSize);
      setInterval(() => { an.getFloatTimeDomainData(buf); let s = 0; for (let i = 0; i < buf.length; i++) s += buf[i] * buf[i];
        window.__tone.push({ t: Date.now(), v: Math.sqrt(s / buf.length) }); }, 4);
    };
  });
}
// Tone bursts on a sample-accurate timeline anchored at the first frame.
function rxBursts(rx, thr) {
  rx = rx.slice().sort((a, b) => a.t - b.t);
  let t = rx[0].t; const out = []; let cur = null;
  for (const p of rx) {
    const ms = p.n / 48;
    if (p.v > thr) { if (!cur) cur = { on: t, off: t + ms }; else if (t - cur.off > 8) { out.push(cur); cur = { on: t, off: t + ms }; } else cur.off = t + ms; }
    t += ms;
  }
  if (cur) out.push(cur);
  return out;
}
function toneBursts(tone, thr) {
  const out = []; let cur = null;
  for (const p of tone) { if (p.v > thr && !cur) cur = { on: p.t }; else if (p.v <= thr && cur) { cur.off = p.t; out.push(cur); cur = null; } }
  if (cur) out.push(cur);
  return out;
}
const modelElements = (page) => page.evaluate(() => ({ el: window.cwKeyer.elements(0), end: window.cwKeyer.base + window.cwKeyer.nextTime, wpm: window.cwSpeed }));
const ngCount = () => { try { return (fs.readFileSync(VR_LOG, 'utf8').match(/CW NG: keyer buffer full/g) || []).length; } catch (e) { return 0; } };

(async () => {
  const browser = await chromium.launch({ executablePath: found.chrome, args: ['--ignore-certificate-errors', '--autoplay-policy=no-user-gesture-required'] });
  const ctx = await browser.newContext({ ignoreHTTPSErrors: true });
  const A = await ctx.newPage(), B = await ctx.newPage();
  const errors = [];
  A.on('pageerror', e => errors.push(String(e)));
  await bringUp(A, URL_A);
  await bringUp(B, URL_B);
  await B.click('#cwDecoderToggle');
  await tapRx(B);
  await tapTone(A);
  await sleep(1500);

  // ---- 1. model vs the rig's keying over ten words --------------------------
  const text = 'CQ CQ CQ DE W1AAA W1AAA W1AAA PSE K';
  await A.evaluate((t) => { window.sendCWText(t); window.__tapStart(); }, text);
  let m = await modelElements(A);
  await sleep(m.end - Date.now() + 2500);
  let rx = await B.evaluate(() => window.__rx);
  const rb = rxBursts(rx, 0.02);
  const lag0 = rb[0].on - m.el[0].on;
  const lags = rb.map(b => { const x = b.on - lag0; let best = m.el[0]; for (const e of m.el) if (Math.abs(e.on - x) < Math.abs(best.on - x)) best = e; return b.on - best.on; });
  const drift = lags[lags.length - 1] - lags[0];
  console.log('model elements ' + m.el.length + ', rig bursts ' + rb.length + ', rig lag ' + lag0.toFixed(0) + ' ms, drift first->last ' + drift.toFixed(0) + ' ms over ' + ((m.end - m.el[0].on) / 1000).toFixed(1) + ' s');
  check(Math.abs(drift) <= 60, 'model keeps step with the rig (drift within 60 ms; a 10-dot word gap would drift ' + (10 * 3 * 1200 / m.wpm).toFixed(0) + ' ms)');
  check(Math.abs(rb.length - m.el.length) <= 6, 'about as many bursts heard as elements modelled');
  await B.waitForFunction(() => document.getElementById('cwDecoderTextInner').textContent.includes('W1AAA'), null, { timeout: 15000 }).catch(() => {});
  const copy1 = await B.evaluate(() => document.getElementById('cwDecoderTextInner').textContent.trim());
  console.log('B copied: ' + JSON.stringify(copy1));
  check(copy1.includes('W1AAA') && copy1.includes('PSE'), 'B copies the text');
  const tone1 = toneBursts(await A.evaluate(() => window.__tone), 0.02);
  check(tone1.length === m.el.length, 'sidetone: one burst per element (' + tone1.length + ' of ' + m.el.length + ')');
  let maxErr = 0; for (let i = 0; i < Math.min(tone1.length, m.el.length); i++) maxErr = Math.max(maxErr, Math.abs(tone1[i].on - m.el[i].on));
  check(maxErr <= 40, 'sidetone within 40 ms of the model (max ' + maxErr.toFixed(0) + ')');

  // ---- 2. CHR-mode type-ahead: no overflow, every character copied ---------
  await A.click('#cwStopBtn');
  await B.evaluate(() => { document.getElementById('cwDecoderTextInner').textContent = ''; });
  const ng0 = ngCount();
  const typed = 'THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 73 DE W1AAA K';
  for (const ch of typed) { await A.evaluate((c) => window.sendCWText(c), ch); await sleep(25); }   // ~40 chars/s
  m = await modelElements(A);
  await sleep(m.end - Date.now() + 3000);
  const copy2 = await B.evaluate(() => document.getElementById('cwDecoderTextInner').textContent.trim().replace(/\s+/g, ' '));
  console.log('B copied: ' + JSON.stringify(copy2));
  check(ngCount() === ng0, 'type-ahead: the rig never rejected a frame (keyer buffer never overflowed)');
  check(copy2.includes('LAZY DOG') && copy2.includes('W1AAA K'), 'type-ahead: B copies the end of the text (nothing lost)');

  // ---- 3. the bench catches an overflow when the pacing is bypassed ---------
  await A.evaluate(() => { for (let i = 0; i < 3; i++) window.cwKeyer._send('EEEEEEEEEEEEEEEEEEEE'); });
  await sleep(1500);
  check(ngCount() > ng0, 'virtual rig NGs a frame that would overflow its keyer buffer (' + (ngCount() - ng0) + ' NG)');
  await A.click('#cwStopBtn');
  await sleep(500);

  // ---- 4. Backspace cuts and the next character starts at once -------------
  await A.evaluate(() => { window.__tone = []; });
  for (const ch of 'CQ CQ CQ DE W1AAA') await A.evaluate((c) => window.sendCWText(c), ch);
  await sleep(700);
  await A.focus('#cwInput');
  await A.keyboard.press('Backspace');
  const bs = await A.evaluate(() => ({ pending: window.cwKeyer.characters.filter(c => c.status === 'pending').length, rewound: Math.abs(window.cwKeyer.nextTime - window.cwKeyer.modelNow()) < 50 }));
  check(bs.pending === 0 && bs.rewound, 'Backspace drops the pending text and rewinds the model clock');
  const tStopped = Date.now();
  await sleep(300);
  const tone4a = (await A.evaluate(() => window.__tone)).filter(p => p.t > tStopped + 60);
  check(tone4a.every(p => p.v <= 0.005), 'Backspace cuts the sidetone');
  await A.evaluate(() => { window.__tone = []; window.sendCWText('K'); });
  const tK = Date.now();
  await sleep(600);
  const tone4b = toneBursts(await A.evaluate(() => window.__tone), 0.02);
  check(tone4b.length === 3 && tone4b[0].on - tK < 80, 'the next character keys at once, tone and all (K = 3 elements, first after ' + (tone4b.length ? (tone4b[0].on - tK).toFixed(0) : '-') + ' ms)');
  await A.click('#cwStopBtn');

  // ---- 5. sidetone across spaces and a pause while typing fast --------------
  await A.evaluate(() => { window.__tone = []; });
  for (const ch of 'THE QUICK BROWN FOX') { await A.evaluate((c) => window.sendCWText(c), ch); await sleep(60); }
  await sleep(900);
  for (const ch of ' JUMPS') { await A.evaluate((c) => window.sendCWText(c), ch); await sleep(60); }
  m = await modelElements(A);
  await sleep(m.end - Date.now() + 700);
  const tone5 = toneBursts(await A.evaluate(() => window.__tone), 0.02);
  check(tone5.length === m.el.length, 'fast typing with spaces and a pause: every element played (' + tone5.length + ' of ' + m.el.length + ')');
  check(await A.evaluate(() => window.cwSidetone.osc === null), 'oscillator parked after the end');

  // ---- 6. STOP cuts the tone -------------------------------------------------
  await A.evaluate(() => { window.__tone = []; window.sendCWText('VVV VVV VVV VVV VVV'); });
  await sleep(1200);
  const tStop = Date.now();
  await A.click('#cwStopBtn');
  await sleep(500);
  const tone6 = await A.evaluate(() => window.__tone);
  check(tone6.some(p => p.t < tStop && p.v > 0.02) && tone6.filter(p => p.t > tStop + 60).every(p => p.v <= 0.005), 'STOP silences the sidetone within 60 ms');

  console.log('page errors: ' + JSON.stringify(errors));
  check(errors.length === 0, 'no page errors');
  await browser.close();
  console.log(fails ? 'FAILED ' + fails : 'ALL PASS');
  process.exit(fails ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });

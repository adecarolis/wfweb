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
// --paddle: section 7 alone, --chr: section 8 alone, both: the two.
const ONLY = { paddle: process.argv.includes('--paddle'), chr: process.argv.includes('--chr') };

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
  // WFWEB_BENCH_WPM=25: key at another speed than the page's default 20
  // (the rig's 0..255 level rounds to WPM; at 25 a truncating virtual rig
  // keyed 24 and drifted ~600 ms over ten words, 2026-11-27).
  const wpm = parseInt(process.env.WFWEB_BENCH_WPM || '', 10);
  if (wpm) {
    await A.evaluate((w) => window.setCwSpeedValue(w), wpm);
    console.log('keyer speed set to ' + wpm + ' WPM');
  }
  await sleep(1500);
  if (ONLY.paddle || ONLY.chr) {
    if (ONLY.paddle) await paddleOnAir(A, B);
    if (ONLY.chr) await chrOnAir(A, B);
    return finish(browser, errors);
  }

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

  await paddleOnAir(A, B);
  await chrOnAir(A, B);
  await finish(browser, errors);
})().catch(e => { console.error(e); process.exit(2); });

async function finish(browser, errors) {
  console.log('page errors: ' + JSON.stringify(errors));
  check(errors.length === 0, 'no page errors');
  await browser.close();
  console.log(fails ? 'FAILED ' + fails : 'ALL PASS');
  process.exit(fails ? 1 : 0);
}

// ---- 7. paddle / straight key: what B hears reads as what A paddled --------
// cw-paddle.js decodes the fist and hands the text to the rig's keyer.  Handed
// over a character at a time, the rig ran dry between letters whenever a
// letter took it less time than the next one took the hand, and the idle
// time went out as a word gap ("C Q DE K 1ABC K", 2026-11-28); a letter
// arriving as the buffer drained went out with no gap at all.  B's received
// audio is read by its gaps at the rig's dot, as an ear would: under two
// dots the same character, under five the next, else a new word.
function rng(seed) { let s = seed >>> 0; return () => ((s = (s * 1664525 + 1013904223) >>> 0) / 2 ** 32); }
// Contacts {t, role, closed} (ms from the start) spelling text: an iambic
// paddle at the keyer's dot with three / seven dot gaps, or a straight key at
// its own dot with every length off by up to +/- jitter.
function contactTimeline(text, dot, straight, jitter, seed) {
  const r = rng(seed), j = () => straight ? 1 + (r() * 2 - 1) * jitter : 1;
  const ev = []; let t = 200, first = true;
  for (const ch of text) {
    if (ch === ' ') { t += 4 * dot * j(); continue; }
    if (!first) t += 3 * dot * j();
    const p = MORSE[ch];
    for (let k = 0; k < p.length; k++) {
      if (k) t += dot * j();
      const len = (p[k] === '.' ? 1 : 3) * dot * j();
      const role = straight ? 'straight' : (p[k] === '.' ? 'left' : 'right');
      if (straight) { ev.push({ t, role, closed: true }); ev.push({ t: t + len, role, closed: false }); }
      else { ev.push({ t: k ? t - 0.5 * dot : t, role, closed: true }); ev.push({ t: t + 0.5 * len, role, closed: false }); }
      t += len;
    }
    first = false;
  }
  return ev;
}
const MORSE = {
  A: '.-', B: '-...', C: '-.-.', D: '-..', E: '.', F: '..-.', G: '--.', H: '....', I: '..', J: '.---', K: '-.-', L: '.-..', M: '--',
  N: '-.', O: '---', P: '.--.', Q: '--.-', R: '.-.', S: '...', T: '-', U: '..-', V: '...-', W: '.--', X: '-..-', Y: '-.--', Z: '--..',
  0: '-----', 1: '.----', 2: '..---', 3: '...--', 4: '....-', 5: '.....', 6: '-....', 7: '--...', 8: '---..', 9: '----.',
};
const FROM_MORSE = Object.fromEntries(Object.entries(MORSE).map(([c, p]) => [p, c]));
function readBursts(raw, dot) {
  // A hole under half a dot is no element gap (the bench's audio path drops
  // a 20 ms frame now and then, which split a dash in two).
  const b = [];
  for (const x of raw) { if (b.length && x.on - b[b.length - 1].off < 0.5 * dot) b[b.length - 1].off = x.off; else b.push({ ...x }); }
  let out = '', pat = '';
  for (let i = 0; i < b.length; i++) {
    if (i) {
      const g = (b[i].on - b[i - 1].off) / dot;
      if (g >= 2) { out += FROM_MORSE[pat] || '*'; pat = ''; if (g >= 5) out += ' '; }
    }
    pat += (b[i].off - b[i].on) / dot < 2 ? '.' : '-';
  }
  return out + (pat ? FROM_MORSE[pat] || '*' : '');
}
async function paddleOnAir(A, B) {
  await A.click('#cwStopBtn');
  await A.evaluate(() => window.setCwSpeedValue(20));
  await sleep(800);
  const cases = [
    { text: 'CQ DE K1ABC K', straight: false, wpm: 20 },
    { text: 'TEST DE W1AW 73 TU', straight: false, wpm: 20 },
    { text: 'CQ DE K1ABC K', straight: true, wpm: 15, seed: 1 },
    { text: 'TEST DE W1AW 73 TU', straight: true, wpm: 25, seed: 2 },
  ];
  for (const c of cases) {
    const dot = await A.evaluate(() => window.cwKeyer.dot());
    const ev = contactTimeline(c.text, c.straight ? 1200 / c.wpm : dot, c.straight, 0.15, c.seed || 1);
    await A.evaluate(() => { window.cwPaddle.straight.dotMs = 0; });   // a new fist: the estimate starts from the rig's dot
    await B.evaluate(() => { window.__rx = []; });
    const copied = () => B.evaluate(() => document.getElementById('cwDecoderTextInner').textContent.replace(/\s+/g, ' '));
    const copy0 = await copied();
    await A.evaluate((ev) => new Promise((res) => {
      for (const e of ev) setTimeout(() => window.paddleContact(e.role, e.closed), e.t);
      setTimeout(res, ev[ev.length - 1].t + 50);
    }), ev);
    await sleep(10 * 1200 / Math.min(c.wpm, 20));    // the last word space is decided
    const end = await A.evaluate(() => window.cwKeyer.base + window.cwKeyer.nextTime);
    await sleep(Math.max(0, end - Date.now()) + 2500);
    const rb = rxBursts(await B.evaluate(() => window.__rx), 0.02).filter(b => b.off - b.on > 0.3 * dot);
    const air = readBursts(rb, dot);
    const copy = (await copied()).slice(copy0.length).trim();   // the decoder keeps its own history
    const decoded = await A.evaluate(() => window.cwKeyer.characters.map(ch => ch.char).join('').trim());
    const how = c.straight ? `straight key at ${c.wpm} WPM (+/-15 %)` : `paddle at ${c.wpm} WPM`;
    console.log(`  A decoded "${decoded}", B's decoder copied "${copy}"`);
    // A's decode runs on page timers, and a headless page stalls one now and
    // then by more than a dot ("NST" for TEST, the gap timer late): one word
    // may go.  The decoder's timing is cw_paddle_sim.mjs's, on a clean clock;
    // what this section is about is the next check.
    const want = c.text.split(' ');
    check(decoded.split(' ').filter((w, i) => w === want[i]).length >= want.length - 1, `${how}: A decodes "${c.text}" ("${decoded}")`);
    check(air === decoded, `${how}, rig at 20: B hears "${air}", what A decoded`);
    if (air !== decoded) console.log('  bursts on / gap in dots: ' + rb.map((b, i) => (i ? ((b.on - rb[i - 1].off) / dot).toFixed(1) + ' ' : '') + '[' + ((b.off - b.on) / dot).toFixed(1) + ']').join(' '));
    await A.click('#cwStopBtn');
    await sleep(500);
  }
}

// ---- 8. CHR typing just behind the keyer: no letters run together --------
// A letter that reaches the rig after the last element of the one before it
// but inside the three-dot character gap finds the keyer empty, and the rig
// starts it at once: the two letters run together (the IC-7300 did, "K*BC",
// 2026-11-28).  The page types here as the CHR box does (sendCWText per
// key), each key from half a dot before to 2.9 dots after the page's model
// says the letter before it ended, a space half a dot later and the letter
// after it four dots on.  WFWEB_BENCH_NO_GAP_HOLD=1 takes the keyer's gap
// hold out, to see the fault.
async function chrOnAir(A, B) {
  await A.click('#cwStopBtn');
  if (process.env.WFWEB_BENCH_NO_GAP_HOLD) await A.evaluate(() => { window.cwKeyer._gapHold = function () { return 0; }; });
  for (const wpm of [20, 30]) {
    await A.evaluate((w) => window.setCwSpeedValue(w), wpm);
    await sleep(800);
    const dot = await A.evaluate(() => window.cwKeyer.dot());
    const text = 'CQ DE K1ABC K1ABC K';
    await B.evaluate(() => { window.__rx = []; });
    await A.evaluate(async ({ text, xs }) => {
      const k = window.cwKeyer, sleep = (ms) => new Promise((r) => setTimeout(r, ms));
      let n = 0, lastEnd = 0;
      for (let i = 0; i < text.length; i++) {
        window.sendCWText(text[i]);
        if (i + 1 >= text.length) break;
        const d = k.dot();
        if (text[i] !== ' ') {
          const j = k.characters.length - 1, c = k.characters[j], prev = j ? k.characters[j - 1].char : null;
          lastEnd = k.base + c.startTime + window.cwTokenDuration(c.char, prev, d) - 3 * d;   // its last element ends
        }
        const x = xs[n++ % xs.length] + (text[i] === ' ' ? 4 : 0) + (text[i + 1] === ' ' ? 0.5 : 0);
        await sleep(Math.max(0, lastEnd + x * d - Date.now()));
      }
    }, { text, xs: [-0.5, -0.2, 0, 0.2, 0.5, 1, 2, 2.9] });
    const end = await A.evaluate(() => window.cwKeyer.base + window.cwKeyer.nextTime);
    await sleep(Math.max(0, end - Date.now()) + 2500);
    const rb = rxBursts(await B.evaluate(() => window.__rx), 0.02).filter(b => b.off - b.on > 0.3 * dot);
    const air = readBursts(rb, dot);
    check(air === text, `CHR at ${wpm} WPM, each key just after the letter before it ends: B hears "${air}"`);
    if (air !== text) console.log('  bursts on / gap in dots: ' + rb.map((b, i) => (i ? ((b.on - rb[i - 1].off) / dot).toFixed(1) + ' ' : '') + '[' + ((b.off - b.on) / dot).toFixed(1) + ']').join(' '));
    await A.click('#cwStopBtn');
    await sleep(500);
  }
  await A.evaluate(() => window.setCwSpeedValue(20));
}

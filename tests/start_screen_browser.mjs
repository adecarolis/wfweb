// The Server build's start screen in a real browser, on the wfweb that
// tests/test_start_screen.py runs against the IC-7300 mock (WFWEB_START_URL).
//
// The start screen is a translucent sheet over the live page: the page is
// connected and drawing underneath, and the sheet takes the one user gesture
// the browser needs to start RX audio. What must hold:
//  - the page is live under it and the card names the station;
//  - a click anywhere on the sheet, a tap, a key or #startBtn puts it away
//    and starts the audio context;
//  - none of those reaches the radio: under the old opaque splash Space was
//    PTT and the arrows tuned;
//  - Tab, modifiers, function keys and Escape leave it up, as do the links
//    and "Tell a friend", which copies the project's address, never ours;
//  - once it is gone the keys are the page's again.
//
// Needs playwright-core and a Chromium (found like agc_browser.mjs);
// `--probe` only reports whether they were found. WFWEB_SHOT_DIR=<dir> saves
// the desktop and phone screens.

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
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

const URL_ = process.env.WFWEB_START_URL;
if (!URL_) { console.error('WFWEB_START_URL is not set: run through tests/test_start_screen.py'); process.exit(2); }
const shots = process.env.WFWEB_SHOT_DIR || null;

let fails = 0;
function check(ok, msg) { console.log((ok ? 'PASS ' : 'FAIL ') + msg); if (!ok) fails++; }

const browser = await chromium.launch({ executablePath: found.chrome, args: ['--ignore-certificate-errors'] });

async function fresh(viewport) {
  const phone = !!viewport && viewport.width < 500;
  const ctx = await browser.newContext({
    ignoreHTTPSErrors: true, viewport: viewport || { width: 1280, height: 800 },
    hasTouch: phone, isMobile: phone, permissions: ['clipboard-read', 'clipboard-write'] });
  const page = await ctx.newPage();
  page.errs = [];
  page.on('pageerror', e => page.errs.push(String(e)));
  await page.goto(URL_);
  await page.waitForFunction(() => window.currentFreq > 0, null, { timeout: 20000 });
  await page.evaluate(() => {
    window.__sent = [];
    const orig = window.send;
    window.send = function (m) { window.__sent.push(m.cmd); return orig.apply(this, arguments); };
  });
  await page.waitForTimeout(600);   // the card's entry animation
  return page;
}
const up = p => p.evaluate(() => startScreenUp());
const gone = async p => { await p.waitForTimeout(700); return p.evaluate(() => document.getElementById('startupOverlay').style.display === 'none'); };
const state = p => p.evaluate(() => ({
  freq: currentFreq, sent: window.__sent.slice(),
  ctx: audioCtx ? audioCtx.state : null, started: !!(audioEnabled || audioStartPending) }));
const audioOn = s => s.started && s.ctx === 'running';
const modeTile = p => p.evaluate(() => { const r = document.getElementById('modeBtn').getBoundingClientRect(); return { x: r.x + r.width / 2, y: r.y + r.height / 2 }; });

try {
  // ---- the sheet over the live page ---------------------------------------
  let p = await fresh();
  const first = await p.evaluate(() => ({
    up: startScreenUp(), freq: currentFreq,
    conn: document.getElementById('connDot').classList.contains('connected'),
    chip: document.getElementById('startRigName').textContent,
    chipOn: document.getElementById('startRig').className,
    bg: getComputedStyle(document.getElementById('startupOverlay')).backgroundImage,
    version: document.getElementById('splashVersion').textContent }));
  check(first.up && first.freq > 0 && first.conn, `the sheet is up over a connected page (freq ${first.freq})`);
  check(/rgba\(/.test(first.bg), 'the sheet is translucent (an rgba scrim, not an opaque fill)');
  check(first.chip.length > 0 && /\bon\b/.test(first.chipOn) && /\blive\b/.test(first.chipOn), `the card names the station, dot live (got "${first.chip}" ${first.chipOn})`);
  check(/^wfweb v\. \d/.test(first.version), `the card shows the version (got "${first.version}")`);
  if (shots) await p.screenshot({ path: path.join(shots, 'start-desktop.png') });
  const f0 = first.freq;

  for (const k of ['Shift', 'Tab', 'Escape', 'F2']) await p.keyboard.press(k);
  check(await up(p), 'Shift, Tab, Escape and F2 leave the sheet up');
  check(!(await state(p)).sent.includes('setPTT'), 'none of them keyed');

  await p.evaluate(() => document.querySelectorAll('#startupOverlay a').forEach(a => a.addEventListener('click', e => e.preventDefault())));
  await p.click('#startupOverlay .start-foot a >> nth=0');
  await p.click('#startupOverlay .start-links a');
  check(await up(p), 'a click on a link leaves the sheet up');
  await p.click('#startShare');
  await p.waitForTimeout(400);
  const shared = await p.evaluate(async () => ({ label: document.getElementById('startShareText').textContent, clip: await navigator.clipboard.readText().catch(e => String(e)) }));
  check(await up(p) && shared.label === 'Link copied' && shared.clip === 'https://github.com/adecarolis/wfweb',
        `Tell a friend copies the project's address and leaves the sheet up (got ${JSON.stringify(shared)})`);

  // ---- an arrow key starts, and does not tune -----------------------------
  await p.evaluate(() => { if (document.activeElement) document.activeElement.blur(); });
  await p.keyboard.press('ArrowUp');
  check(!(await up(p)), 'ArrowUp puts the sheet away');
  check(await gone(p), 'the sheet leaves the layout after its fade');
  let s = await state(p);
  check(s.freq === f0 && !s.sent.includes('setFrequency'), `ArrowUp under the sheet did not tune (${f0} -> ${s.freq}, sent ${s.sent.join()})`);
  check(audioOn(s), `the key started the audio context (ctx ${s.ctx}, started ${s.started})`);
  await p.keyboard.press('ArrowUp');
  await p.waitForTimeout(500);
  s = await state(p);
  check(s.sent.includes('setFrequency'), 'with the sheet gone ArrowUp tunes again');
  await p.keyboard.press('ArrowDown');   // leave the mock where it was
  await p.waitForTimeout(500);
  check(p.errs.length === 0, `no page errors (${p.errs.join(' | ')})`);
  await p.context().close();

  // ---- Space starts, and does not key --------------------------------------
  p = await fresh();
  await p.keyboard.press('Space');
  await p.waitForTimeout(800);
  s = await state(p);
  check(!(await up(p)) && audioOn(s), `Space starts (ctx ${s.ctx})`);
  check(!s.sent.includes('setPTT'), `Space under the sheet did not key (sent ${s.sent.join()})`);
  await p.context().close();

  // ---- a click anywhere on the sheet, here over the MODE tile --------------
  p = await fresh();
  const at = await modeTile(p);
  await p.mouse.click(at.x, at.y);
  await p.waitForTimeout(800);
  s = await state(p);
  check(!(await up(p)) && audioOn(s), `a click on the scrim starts (ctx ${s.ctx})`);
  check(!(await p.evaluate(() => document.getElementById('modeOverlay').classList.contains('visible'))), 'that click did not reach the MODE tile under it');
  // the page takes clicks again at once
  // (measured again: the top bar shifts as the rig's tags arrive, TUNE for one)
  const at2 = await modeTile(p);
  await p.mouse.click(at2.x, at2.y);
  await p.waitForTimeout(300);
  check(await p.evaluate(() => document.getElementById('modeOverlay').classList.contains('visible')), 'the next click reaches the page');
  await p.context().close();

  // ---- the button, as the other browser tests press it ---------------------
  p = await fresh();
  await p.click('#startBtn');
  await p.waitForTimeout(800);
  s = await state(p);
  check(!(await up(p)) && audioOn(s), `#startBtn starts (ctx ${s.ctx})`);
  await p.context().close();

  // ---- phone ---------------------------------------------------------------
  p = await fresh({ width: 390, height: 844 });
  if (shots) await p.screenshot({ path: path.join(shots, 'start-phone.png') });
  const fit = await p.evaluate(() => { const r = document.querySelector('#startupOverlay .start-card').getBoundingClientRect(); return { l: r.left, r: r.right, t: r.top, b: r.bottom, w: innerWidth, h: innerHeight, wide: document.documentElement.scrollWidth }; });
  check(fit.l >= 0 && fit.r <= fit.w && fit.t >= 0 && fit.b <= fit.h && fit.wide <= fit.w, `the card fits a 390 px phone (${JSON.stringify(fit)})`);
  await p.tap('#startupOverlay', { position: { x: 20, y: 20 } });
  await p.waitForTimeout(800);
  s = await state(p);
  check(!(await up(p)) && audioOn(s), `a tap on the scrim starts (ctx ${s.ctx})`);
  await p.context().close();
} catch (e) {
  console.log('FAIL exception: ' + (e && e.stack || e));
  fails++;
} finally {
  await browser.close();
}
console.log(fails ? `${fails} FAILED` : 'all passed');
process.exit(fails ? 1 : 0);

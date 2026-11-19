// The click that lands while our own CQ is going out. A calls CQ, B answers;
// A's decode of the answer comes in at the top of A's next slot, the auto-CQ
// starts, and the operator's click on the answer lands a moment later. The
// answer used to wait for the end of that CQ (single click) or for the next
// cycle with the slot left silent (double click), so A skipped a whole cycle.
// Now the click cuts the CQ and A's report goes out in the same slot, its
// opening cut off so it stays on the slot's timeline, and B has to decode it
// there. Past digiLateStartLimit() in the slot the CQ runs to its end, the
// report goes out a cycle later, and that CQ counts as no try at the report.
//
// That second QSO then runs on to A's RR73, and A's operator clicks the next
// caller of the pile-up while it is on the air. The QSO with B is complete and
// has to be in A's log, once, with the reports of that QSO: it used to be
// logged only when B's 73 came in with B still the partner, so a run lost
// every QSO (2026-11-16).
// FT8, two Standalone pages on ?virtual=1, never a real radio.
//
// With WFWEB_FT8_URLS="https://127.0.0.1:9080 https://127.0.0.1:9090" the same
// run goes over two wfweb servers on the testrig bench (scripts/testrig.sh up 2).
// With WFWEB_FT8_LOGIN="name:password" as well, those servers run --shared
// (TESTRIG_SHARED=1 scripts/testrig.sh up 2): each page logs in with that
// account, takes the seat and works under the call the server puts on the air
// (the station call, or the account's own), which the operator cannot type.
//
// Needs playwright-core and a Chromium (found like comp_browser.mjs); `--probe`
// only reports whether they were found. Run through tests/test_ft8_takeover.py.
// WFWEB_SHOT_DIR=<dir> saves both windows.

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

const SERVER_URLS = (process.env.WFWEB_FT8_URLS || '').split(/\s+/).filter(Boolean);
const onServer = SERVER_URLS.length === 2;
const dist = onServer ? null : fs.mkdtempSync(path.join(os.tmpdir(), 'wfweb-ft8take-'));
if (!onServer) execFileSync('bash', [path.join(ROOT, 'tools', 'build-static.sh'), dist], { stdio: 'ignore' });
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css', '.wasm': 'application/wasm', '.svg': 'image/svg+xml', '.png': 'image/png', '.json': 'application/json' };
const server = http.createServer((req, res) => {
  let p = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  if (p.endsWith('/')) p += 'index.html';
  const f = path.join(dist, path.normalize(p));
  if (!f.startsWith(dist) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': TYPES[path.extname(f)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
  fs.createReadStream(f).pipe(res);
});
if (!onServer) await new Promise(r => server.listen(0, '127.0.0.1', r));
const rigUrl = (id) => onServer ? SERVER_URLS[id - 1] + '/' : `http://127.0.0.1:${server.address().port}/?virtual=1&id=${id}`;

let CQER = 'K1ABC', CALLER = 'VA2BBW';
const THIRD = 'W9XYZ';
const LOGIN = onServer && process.env.WFWEB_FT8_LOGIN ? process.env.WFWEB_FT8_LOGIN.split(':') : null;
const browser = await chromium.launch({
  executablePath: found.chrome, headless: true,
  args: ['--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream', '--autoplay-policy=no-user-gesture-required',
         '--disable-background-timer-throttling', '--disable-renderer-backgrounding', '--disable-backgrounding-occluded-windows'],
});
const shots = process.env.WFWEB_SHOT_DIR || null;
try {
  const newCtx = () => browser.newContext({ viewport: { width: 1200, height: 820 }, ignoreHTTPSErrors: true });
  const ctx = await newCtx();
  async function rig(id, call, grid) {
    // Shared Station: a session cookie per server, and cookies ignore the
    // port, so each rig gets a context of its own.
    const own = LOGIN ? await newCtx() : ctx;
    if (LOGIN) {
      const r = await own.request.post(rigUrl(id) + 'api/v1/auth/login', { data: { username: LOGIN[0], password: LOGIN[1] } });
      check(r.ok(), `rig ${id}: logged in as ${LOGIN[0]} (${r.status()})`);
    }
    const page = await own.newPage();
    page.on('pageerror', (e) => { console.log(`pageerror (${call}): ` + e.message); fails++; });
    page.on('console', (m) => { if (process.env.WFWEB_DEBUG ? /\[DIGI/.test(m.text()) : /\[DIGI\] take over|dblclick: aborting/.test(m.text())) console.log(`  ${call}: ${m.text()}`); });
    const url = rigUrl(id);
    await page.goto(url);
    // One origin on Standalone: the second rig clearing storage would wipe the
    // first's call.
    if (id === 1 || onServer) { await page.evaluate(() => { localStorage.clear(); sessionStorage.clear(); }); await page.goto(url); }
    await page.click('#startBtn');
    await page.waitForFunction(() => window.currentFreq > 0 && window.audioEnabled === true, null, { timeout: 30000 });
    if (LOGIN) {
      await page.evaluate(() => window.send({ cmd: 'seatTake' }));
      const mine = await page.waitForFunction(() => window.seatState && window.seatState.mine, null, { timeout: 10000 }).then(() => true, () => false);
      check(mine, `rig ${id}: has the seat`);
    }
    await page.evaluate(([call, grid, shared]) => {
      window.toggleDigiBar();
      // Shared Station: the call and the grid are the server's.
      if (!shared) window.App.callsign.set(call);
      const g = document.getElementById('digiGrid');
      if (!shared) { g.value = grid; g.dispatchEvent(new Event('input', { bubbles: true })); }
      if (window.digiMode !== 'FT8') document.getElementById('digiModeLabel').click();
      // Every over, as encoded, with the slot and how far into it it began.
      window.__tx = [];
      const lib = window.ft8tsLib, enc = lib.encodeFT8;
      lib.encodeFT8 = function (m, o) {
        if (window.digiTxActive) { const s = window.getDigiSlotInfo(); window.__tx.push({ m, slot: s.slotIndex, phase: +s.slotPhase.toFixed(2) }); }
        return enc.call(this, m, o);
      };
    }, [call, grid, !!LOGIN]);
    await page.waitForFunction(() => window.ft8tsLib && window.digiBarVisible && window.digiMode === 'FT8', null, { timeout: 10000 });
    return page;
  }
  const A = await rig(1, CQER, 'FN42'), B = await rig(2, CALLER, 'FN35');
  let gridA = 'FN42', gridB = 'FN35';
  if (LOGIN) {
    const id = (p) => p.evaluate(() => [window.App.callsign.get(), document.getElementById('digiGrid').value]);
    [CQER, gridA] = await id(A); [CALLER, gridB] = await id(B);
    check(!!CQER && !!CALLER && !!gridA && !!gridB, `on the air as ${CQER} ${gridA} and ${CALLER} ${gridB}`);
  }
  await A.waitForTimeout(1000);

  // Double-click the newest decode that matches, as an operator would.
  const pick = async (page, who, re) => {
    await page.waitForFunction((src) => {
      const r = new RegExp(src);
      return [...document.querySelectorAll('#digiRxPanel .digi-decode-row')].some(e => r.test(decodeURIComponent(e.getAttribute('data-msg'))));
    }, re.source, { timeout: 45000 }).catch(() => {});
    const m = await page.evaluate((src) => {
      const r = new RegExp(src);
      const rows = [...document.querySelectorAll('#digiRxPanel .digi-decode-row')].filter(e => r.test(decodeURIComponent(e.getAttribute('data-msg'))));
      if (!rows.length) return null;
      const el = rows[rows.length - 1];
      el.dispatchEvent(new MouseEvent('dblclick', { bubbles: true }));
      return decodeURIComponent(el.getAttribute('data-msg'));
    }, re.source);
    check(!!m, `${who} double-clicks ${m}`);
    return m;
  };
  // A's operator: once the answer is on screen and the auto-CQ is on the air
  // between `from` and `to` seconds into its slot, click the answer. Done in
  // the page so the click lands where it is meant to.
  const clickDuringCq = (page, re, from, to) => page.evaluate(([src, from, to]) => new Promise((resolve) => {
    const r = new RegExp(src);
    const t0 = Date.now();
    (function poll() {
      const rows = [...document.querySelectorAll('#digiRxPanel .digi-decode-row')].filter(e => r.test(decodeURIComponent(e.getAttribute('data-msg'))));
      const s = window.getDigiSlotInfo();
      if (rows.length && window.digiTxActive && window.digiTxOnAir && /^CQ /.test(window.digiTxOnAir.msg) &&
          s.slotPhase >= from && s.slotPhase < to) {
        rows[rows.length - 1].click();
        resolve({ slot: s.slotIndex, phase: +s.slotPhase.toFixed(2), active: window.digiTxActive,
                  onAir: window.digiTxOnAir && window.digiTxOnAir.msg, queued: window.digiTxQueued });
        return;
      }
      if (Date.now() - t0 > 75000) { resolve(null); return; }
      setTimeout(poll, 20);
    })();
  }), [re.source, from, to]);
  const logged = (page, call) => page.waitForFunction((c) => [...document.querySelectorAll('#digiDirectedPanel .digi-logged-row')].some(e => e.textContent.includes('LOGGED ' + c + ' ')), call, { timeout: 90000 }).then(() => true, () => false);
  const reportRe = new RegExp(`^${CALLER} ${CQER} [-+]\\d{1,2}$`);

  // ---- early in the slot: the answer takes the CQ's place
  await A.evaluate(() => window.digiCallCq());
  await pick(B, 'B', new RegExp(`^CQ ${CQER} ${gridA}$`));
  const c = await clickDuringCq(A, new RegExp(`^${CQER} ${CALLER} ${gridB}$`), 0.3, 2.5);
  check(!!c, `A clicks B's answer ${c ? c.phase + ' s into its CQ over' : '(never saw it during a CQ)'}`);
  if (c) {
    check(c.active && reportRe.test(c.onAir || ''), `the report is on the air at once (${c.onAir})`);
    const tx = await A.evaluate(() => window.__tx);
    const cq = tx.filter(t => /^CQ /.test(t.m)).pop(), rep = tx.find(t => reportRe.test(t.m));
    check(cq && rep && rep.slot === cq.slot, `the report went out in the CQ's slot (${JSON.stringify(cq)} -> ${JSON.stringify(rep)})`);
    // B decodes the cut report in that slot.
    const heard = await B.waitForFunction(([re, slot]) => window.digiDecodes.some(d => new RegExp(re).test(d.msg) && d.slotIndex === slot),
      [reportRe.source, c.slot], { timeout: 25000 }).then(() => true, () => false);
    check(heard, `B decoded the report from slot ${c.slot}, its opening cut by ${c.phase} s`);
    if (shots) { await A.screenshot({ path: path.join(shots, 'ft8take-A.png') }); await B.screenshot({ path: path.join(shots, 'ft8take-B.png') }); }
    const okA = await logged(A, CALLER), okB = await logged(B, CQER);
    console.log('A sent', JSON.stringify(await A.evaluate(() => window.__tx)));
    console.log('B sent', JSON.stringify(await B.evaluate(() => window.__tx)));
    check(okA && okB, `the QSO carries on to the log on both sides (A ${okA}, B ${okB})`);
    // A logged it as its RR73 went out; B's 73 after that is the same QSO.
    await A.waitForFunction(() => window.digiQsoState === 'IDLE', null, { timeout: 45000 }).catch(() => {});
    const rows = await A.evaluate((c) => [...document.querySelectorAll('#digiDirectedPanel .digi-logged-row')].filter(e => e.textContent.includes('LOGGED ' + c + ' ')).length, CALLER);
    check(rows === 1, `A logged it once, B's 73 added no second entry (${rows})`);
  }

  // ---- too late in the slot: the CQ runs out, the report waits a cycle
  for (const p of [A, B]) {
    await p.waitForFunction(() => window.digiQsoState === 'IDLE' && !window.digiTxActive, null, { timeout: 30000 }).catch(() => {});
    await p.evaluate(() => {
      window.haltDigiTx();
      window.digiTxEnabled = false; window.digiTxArmed = false; window.digiTxQueued = null;
      window.digiQsoState = 'IDLE'; window.digiQsoPartner = ''; window.__tx = [];
      window.digiLastAutoLog = null;   // the same two stations work each other again below
      document.getElementById('digiDxCall').value = '';
      window.digiDecodes.length = 0;
      window.renderDigiDecodes();
      window.updateDigiMsgRows();
    });
  }
  await A.evaluate(() => window.digiCallCq());
  await pick(B, 'B', new RegExp(`^CQ ${CQER} ${gridA}$`));
  const limit = await A.evaluate(() => window.digiLateStartLimit());
  const late = await clickDuringCq(A, new RegExp(`^${CQER} ${CALLER} ${gridB}$`), limit + 0.3, limit + 3);
  check(!!late, `A clicks B's answer ${late ? late.phase + ' s into its CQ over' : '(never saw it during a CQ)'}`);
  if (late) {
    check(late.active && /^CQ /.test(late.onAir || '') && reportRe.test(late.queued || ''), `past ${limit} s the CQ stays on the air, the report queued`);
    await A.waitForFunction(() => !window.digiTxActive, null, { timeout: 20000 });
    const st = await A.evaluate(() => ({ attempts: window.digiTxAttempts, armed: window.digiTxArmed, queued: window.digiTxQueued }));
    check(st.attempts === 0 && st.armed && reportRe.test(st.queued || ''), `that CQ was no try at the report (${JSON.stringify(st)})`);
    await A.waitForFunction((re) => window.__tx.some(t => new RegExp(re).test(t.m)), reportRe.source, { timeout: 40000 }).catch(() => {});
    const tx = await A.evaluate(() => window.__tx);
    const cq = tx.filter(t => /^CQ /.test(t.m)).pop(), rep = tx.find(t => reportRe.test(t.m));
    check(cq && rep && rep.slot === cq.slot + 2, `the report went out on the next cycle (${JSON.stringify(cq)} -> ${JSON.stringify(rep)})`);

    // ---- the next caller clicked while our RR73 is on the air
    // The QSO runs on by itself: B rogers the report, A sends RR73. A third
    // station has been calling all along; A's operator clicks it too late in
    // the RR73 for a take-over, so the RR73 runs out and the QSO with B is done.
    const entries = (page, call) => page.evaluate((c) => window.qsoLog.filter(q => q.call === c).map(q => [q.rstSent, q.rstRcvd, q.df]), call);
    const a0 = (await entries(A, CALLER)).length, b0 = (await entries(B, CQER)).length;
    const next = await A.evaluate(([cqer, third, from, to]) => new Promise((resolve) => {
      const t0 = Date.now();
      (function poll() {
        const s = window.getDigiSlotInfo();
        if (window.digiTxActive && window.digiTxOnAir && /\sRR73$/.test(window.digiTxOnAir.msg) && s.slotPhase >= from && s.slotPhase < to) {
          window.digiDecodes.push({ time: new Date().toISOString().substr(11, 8), snr: -11, dt: 0.1, freq: 1900,
                                    msg: cqer + ' ' + third + ' EN61', slotIndex: s.slotIndex - 1 });
          window.renderDigiDecodes();
          const rows = [...document.querySelectorAll('#digiRxPanel .digi-decode-row')].filter(e => decodeURIComponent(e.getAttribute('data-msg')).includes(' ' + third + ' '));
          rows[rows.length - 1].click();
          resolve({ phase: +s.slotPhase.toFixed(2), onAir: window.digiTxOnAir && window.digiTxOnAir.msg, queued: window.digiTxQueued,
                    partner: window.digiQsoPartner, state: window.digiQsoState });
          return;
        }
        if (Date.now() - t0 > 90000) { resolve(null); return; }
        setTimeout(poll, 20);
      })();
    }), [CQER, THIRD, limit + 0.3, limit + 3]);
    check(!!next, `A clicks the next caller ${next ? next.phase + ' s into its RR73 over' : '(never saw its RR73 on the air)'}`);
    if (next) {
      check(/\sRR73$/.test(next.onAir || '') && next.partner === THIRD && next.state === 'ANSWERED' && next.queued === `${THIRD} ${CQER} -11`,
        `the RR73 stays on the air, the report to ${THIRD} queued (${JSON.stringify(next)})`);
      await A.waitForFunction((n) => window.qsoLog.filter(q => q.call === n[0]).length > n[1], [CALLER, a0], { timeout: 30000 }).catch(() => {});
      const mine = await entries(A, CALLER);
      check(mine.length === a0 + 1, `A logged ${CALLER} as the RR73 ended, the partner already ${THIRD} (${a0} -> ${mine.length})`);
      // The reports are those of the QSO with B, not of the station just clicked
      // (-11, and no report received); the decode prints R+03 for the R+3 B sent.
      const sentA = (await A.evaluate(() => window.__tx)).map(t => t.m), sentB = (await B.evaluate(() => window.__tx)).map(t => t.m);
      const rptA = (sentA.filter(m => reportRe.test(m)).pop() || '').split(' ').pop();
      const rptB = (sentB.filter(m => new RegExp(`^${CQER} ${CALLER} R[-+]\\d{1,2}$`).test(m)).pop() || '').split(' ').pop().replace(/^R/, '');
      const e = mine.length === a0 + 1 ? mine[0] : null;   // newest first
      check(!!rptA && !!rptB && !!e && parseInt(e[0], 10) === parseInt(rptA, 10) && parseInt(e[1], 10) === parseInt(rptB, 10) && e[2] !== 1900,
        `with the reports and the audio offset of that QSO (sent ${rptA}, received ${rptB}: ${JSON.stringify(mine)})`);
      // B's 73 comes in for a partner A has left: still one entry.
      const got73 = await A.waitForFunction((re) => window.digiDecodes.some(d => new RegExp(re).test(d.msg)), `^${CQER} ${CALLER} 73$`, { timeout: 45000 }).then(() => true, () => false);
      await A.waitForTimeout(1500);
      const after = (await entries(A, CALLER)).length;
      check(got73 && after === a0 + 1, `B's 73 adds no second entry (73 decoded ${got73}, entries ${after})`);
      const b1 = (await entries(B, CQER)).length;
      check(b1 === b0 + 1, `B logged it too (${b0} -> ${b1})`);
      if (shots) await A.screenshot({ path: path.join(shots, 'ft8take-A-rr73.png') });
    }
    await A.evaluate(() => { window.haltDigiTx(); window.digiTxEnabled = false; window.digiTxArmed = false; window.digiTxQueued = null; });
  }
  // ---- CQ: FIRST: the caller is answered with no click at all
  // (WSJT-X's "Call 1st"). A's TX offset stays where its CQ is.
  for (const p of [A, B]) {
    await p.waitForFunction(() => !window.digiTxActive, null, { timeout: 30000 }).catch(() => {});
    await p.evaluate(() => {
      window.haltDigiTx();
      window.digiTxEnabled = false; window.digiTxArmed = false; window.digiTxQueued = null;
      window.digiQsoState = 'IDLE'; window.digiQsoPartner = ''; window.__tx = [];
      window.digiLastAutoLog = null;
      document.getElementById('digiDxCall').value = '';
      window.digiDecodes.length = 0;
      window.renderDigiDecodes();
      window.updateDigiMsgRows();
    });
  }
  const off = await A.evaluate(() => document.getElementById('digiCqAnswerBtn').textContent);
  await A.click('#digiCqAnswerBtn');
  const on = await A.evaluate(() => [document.getElementById('digiCqAnswerBtn').textContent, window.digiCqAnswer, localStorage.getItem('wfDigiCqAnswer')]);
  check(off === 'CQ: NONE' && on[0] === 'CQ: FIRST' && on[1] === 'first' && on[2] === 'first', `CQ: FIRST is off until pressed (${off} -> ${JSON.stringify(on)})`);
  const txf = await A.evaluate(() => window.digiTxFreq);
  const n0 = await A.evaluate((c) => window.qsoLog.filter(q => q.call === c).length, CALLER);
  await A.evaluate(() => window.digiCallCq());
  await pick(B, 'B', new RegExp(`^CQ ${CQER} ${gridA}$`));
  const auto = await A.waitForFunction((re) => window.__tx.some(t => new RegExp(re).test(t.m)), reportRe.source, { timeout: 60000 }).then(() => true, () => false);
  const st = await A.evaluate(() => ({ partner: window.digiQsoPartner, dx: document.getElementById('digiDxCall').value, txf: window.digiTxFreq, tx: window.__tx }));
  check(auto && st.partner === CALLER && st.dx === CALLER, `A answered ${CALLER} by itself (${JSON.stringify(st.tx.map(t => t.m))})`);
  check(st.txf === txf, `A's TX offset stayed on its CQ (${txf} -> ${st.txf})`);
  await A.waitForFunction((n) => window.qsoLog.filter(q => q.call === n[0]).length > n[1], [CALLER, n0], { timeout: 90000 }).catch(() => {});
  const n1 = await A.evaluate((c) => window.qsoLog.filter(q => q.call === c).length, CALLER);
  check(n1 === n0 + 1, `and the QSO ran to A's log without a click (${n0} -> ${n1})`);
  // B still sends its first message for a moment (bench: its 73); a station just
  // logged is not answered again.
  await A.waitForTimeout(1000);
  // Asked directly, with a CQ in hand and three callers in one batch (no
  // grid, near, far): FIRST takes the first decoded, MAX DIST the farthest,
  // neither the station just logged.
  await A.click('#digiCqAnswerBtn');
  const label = await A.evaluate(() => [document.getElementById('digiCqAnswerBtn').textContent, window.digiCqAnswer]);
  const picks = await A.evaluate(([me, logged, g]) => {
    window.haltDigiTx();
    const batch = [me + ' ' + logged + ' PM95', me + ' W9XYZ', me + ' K2DEF FN30', me + ' JA1XYZ PM95', me + ' DL1ABC JO62']
      .map((msg, k) => ({ msg, freq: 1200 + 100 * k, snr: -5, dt: 0.1 }));
    const run = (mode) => {
      window.digiCqAnswer = mode; window.digiQsoState = 'IDLE'; window.digiQsoPartner = '';
      window.digiTxEnabled = true; window.digiTxQueued = 'CQ ' + me + ' ' + g;
      const ok = window.digiCqAutoAnswer(batch, window.getDigiSlotInfo().slotIndex);
      return [ok, window.digiQsoPartner, window.digiQsoState];
    };
    const out = { first: run('first'), dist: run('dist'), none: run('none') };
    out.km = [window.digiGridKm('FN42', 'FN35'), window.digiGridKm('FN42', 'PM95'), window.digiGridKm('FN42', ''), window.digiGridKm('FN42', 'RR73')].map(Math.round);
    window.digiCqAnswer = 'dist';
    return out;
  }, [CQER, CALLER, gridA]);
  check(label[0] === 'CQ: MAX DIST' && label[1] === 'dist', `the second press is CQ: MAX DIST (${JSON.stringify(label)})`);
  check(picks.first[0] && picks.first[1] === 'W9XYZ' && picks.first[2] === 'ANSWERED', `FIRST takes the first caller decoded, not the one just logged (${JSON.stringify(picks.first)})`);
  check(picks.dist[0] && picks.dist[1] === 'JA1XYZ', `MAX DIST takes the most distant caller (${JSON.stringify(picks.dist)})`);
  check(picks.none[0] === false && picks.none[1] === '', `NONE answers nobody (${JSON.stringify(picks.none)})`);
  check(picks.km[0] > 300 && picks.km[0] < 450 && picks.km[1] > 10000 && picks.km[1] < 11500 && picks.km[2] === -1 && picks.km[3] === -1, `grid distances are sane (${JSON.stringify(picks.km)})`);
  await A.click('#digiCqAnswerBtn');
  const back = await A.evaluate(() => [document.getElementById('digiCqAnswerBtn').textContent, localStorage.getItem('wfDigiCqAnswer')]);
  check(back[0] === 'CQ: NONE' && back[1] === 'none', `the third press is back to CQ: NONE (${JSON.stringify(back)})`);
  for (const p of [A, B]) await p.evaluate(() => { window.haltDigiTx(); window.digiTxEnabled = false; window.digiTxArmed = false; window.digiTxQueued = null; });
  // Leave the seat free: a holder that just vanishes keeps it for the grace time.
  if (LOGIN) for (const p of [A, B]) { await p.evaluate(() => window.send({ cmd: 'seatRelease' })); await p.waitForTimeout(300); }
} finally {
  await browser.close();
  if (!onServer) { server.close(); fs.rmSync(dist, { recursive: true, force: true }); }
}
console.log(fails ? `${fails} FAILED` : 'all passed');
process.exit(fails ? 1 : 0);

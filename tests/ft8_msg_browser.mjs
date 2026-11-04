// A whole FT4 QSO between a nonstandard call and a standard one (#153), in a
// real browser: two Standalone pages on ?virtual=1, never a real radio. The
// page's own sequencer has to carry it through the hashed forms, both ways
// round: the standard station answering the nonstandard CQ (the DXpedition
// case the report came from) and the nonstandard station answering a standard
// CQ. Each side must end up with the QSO logged under the plain call. A third
// QSO between two standard calls guards the everyday path.
//
// With WFWEB_FT8_URLS="https://127.0.0.1:9080 https://127.0.0.1:9090" the same
// run goes over two wfweb servers on the testrig bench (scripts/testrig.sh up 2).
//
// Needs playwright-core and a Chromium (found like comp_browser.mjs); `--probe`
// only reports whether they were found. Run through tests/test_ft8_msg.py.
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
const dist = onServer ? null : fs.mkdtempSync(path.join(os.tmpdir(), 'wfweb-ft8msg-'));
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

const NONSTD = 'AO40EUDXF', STD = 'VA2BBW';
const browser = await chromium.launch({
  executablePath: found.chrome, headless: true,
  args: ['--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream', '--autoplay-policy=no-user-gesture-required',
         '--disable-background-timer-throttling', '--disable-renderer-backgrounding', '--disable-backgrounding-occluded-windows'],
});
const shots = process.env.WFWEB_SHOT_DIR || null;
try {
  const ctx = await browser.newContext({ viewport: { width: 1200, height: 820 }, ignoreHTTPSErrors: true });
  async function rig(id, call, grid) {
    const page = await ctx.newPage();
    page.on('pageerror', (e) => { console.log(`pageerror (${call}): ` + e.message); fails++; });
    const url = rigUrl(id);
    await page.goto(url);
    // One origin on Standalone: the second rig clearing storage would wipe the
    // first's call.
    if (id === 1 || onServer) { await page.evaluate(() => { localStorage.clear(); sessionStorage.clear(); }); await page.goto(url); }
    await page.click('#startBtn');
    await page.waitForFunction(() => window.currentFreq > 0 && window.audioEnabled === true, null, { timeout: 30000 });
    await page.evaluate(([call, grid]) => {
      window.toggleDigiBar();
      window.App.callsign.set(call);
      const g = document.getElementById('digiGrid');
      g.value = grid; g.dispatchEvent(new Event('input', { bubbles: true }));
      if (window.digiMode !== 'FT4') document.getElementById('digiModeLabel').click();
      // Every message that went out, as encoded.
      window.__tx = [];
      const lib = window.ft8tsLib, enc = lib.encodeFT4;
      lib.encodeFT4 = function (m, o) { if (window.digiTxActive) window.__tx.push(m); return enc.call(this, m, o); };
    }, [call, grid]);
    await page.waitForFunction(() => window.ft8tsLib && window.digiBarVisible && window.digiMode === 'FT4', null, { timeout: 10000 });
    return page;
  }
  const A = await rig(1, NONSTD, 'JJ00'), B = await rig(2, STD, 'FN35');
  await A.waitForTimeout(1000);

  const reset = (page) => page.evaluate(() => {
    window.digiHalt && window.digiHalt();
    window.digiTxEnabled = false; window.digiTxArmed = false; window.digiTxQueued = null;
    window.digiQsoState = 'IDLE'; window.digiQsoPartner = ''; window.__tx = [];
    document.getElementById('digiDxCall').value = '';
    document.querySelectorAll('#digiDirectedPanel .digi-logged-row').forEach(e => e.remove());
    window.digiDecodes.length = 0; window.updateDigiMsgRows();
  });
  // Double-click the newest decode that matches, as an operator would.
  const pick = async (page, who, re) => {
    await page.waitForFunction((src) => {
      const r = new RegExp(src);
      return window.digiDecodes.some(d => r.test(d.msg)) && [...document.querySelectorAll('#digiRxPanel .digi-decode-row')].some(e => r.test(decodeURIComponent(e.getAttribute('data-msg'))));
    }, re.source, { timeout: 40000 }).catch(() => {});
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
  const logged = (page, call) => page.waitForFunction((c) => [...document.querySelectorAll('#digiDirectedPanel .digi-logged-row')].some(e => e.textContent.includes('LOGGED ' + c + ' ')), call, { timeout: 75000 }).then(() => true, () => false);
  const dump = async (page) => page.evaluate(() => ({ tx: window.__tx, state: window.digiQsoState, queued: window.digiTxQueued, rx: window.digiDecodes.map(d => d.msg) }));

  // ---- the standard station answers the nonstandard CQ
  await A.evaluate(() => window.digiCallCq());
  check(await A.evaluate(() => window.digiTxQueued) === `CQ ${NONSTD}`, 'A calls a CQ with no grid, which the message has no room for');
  await pick(B, 'B', new RegExp(`^CQ ${NONSTD}$`));
  check(await B.evaluate(() => document.getElementById('digiDxCall').value) === NONSTD, 'B: the DX field holds the plain call');
  check(await B.evaluate(() => window.digiTxQueued) === `<${NONSTD}> ${STD} FN35`, `B calls with the DX hashed, not "${NONSTD} ${STD.slice(0, 3)}"`);
  check(await B.evaluate(() => [...document.querySelectorAll('#digiMsgRows .digi-msg-row.armed')].length) === 1, 'B: the armed message is one of the rows');
  await pick(A, 'A', new RegExp(`^<${NONSTD}> ${STD} FN35$`));
  const okA = await logged(A, STD), okB = await logged(B, NONSTD);
  const da = await dump(A), db = await dump(B);
  console.log('A sent', JSON.stringify(da.tx)); console.log('B sent', JSON.stringify(db.tx));
  check(okA, `A logged ${STD}`); check(okB, `B logged ${NONSTD}`);
  check(!db.tx.concat(da.tx).some(m => m === `${NONSTD} ${STD.slice(0, 3)}` || new RegExp(`^${NONSTD} ${STD}`).test(m)), 'no message went out as two plain calls');
  check(db.tx.includes(`AO40EUDXF <${STD}> 73`), 'B signed with the 73 in the nonstandard form');
  check(da.rx.some(m => m === `${NONSTD} <${STD}> 73`), 'A resolved the hash of B out of the 73');
  check(db.rx.some(m => m === `<${STD}> ${NONSTD} RR73`), 'B resolved its own hash out of the RR73');
  if (shots) { await A.screenshot({ path: path.join(shots, 'ft8msg-nonstd.png') }); await B.screenshot({ path: path.join(shots, 'ft8msg-std.png') }); }
  const rep = await B.evaluate((c) => { const q = window.qsoLog.find(q => q.call === c); return q ? [q.rstSent, q.rstRcvd] : null; }, NONSTD);
  check(rep && /^[-+]\d+$/.test(rep[0]) && /^[-+]\d+$/.test(rep[1]), `B logged both reports (${rep})`);

  // ---- the other way round: the nonstandard station answers a standard CQ
  await A.waitForFunction(() => window.digiQsoState === 'IDLE' && !window.digiTxActive, null, { timeout: 20000 }).catch(() => {});
  await B.waitForFunction(() => window.digiQsoState === 'IDLE' && !window.digiTxActive, null, { timeout: 20000 }).catch(() => {});
  await reset(A); await reset(B);
  await B.evaluate(() => window.digiCallCq());
  await pick(A, 'A', new RegExp(`^CQ ${STD} FN35$`));
  check(await A.evaluate(() => window.digiTxQueued) === `<${STD}> ${NONSTD}`, 'A answers a standard CQ in the two-call form');
  await pick(B, 'B', new RegExp(`^<${STD}> ${NONSTD}$`));
  const okA2 = await logged(A, STD), okB2 = await logged(B, NONSTD);
  const da2 = await dump(A), db2 = await dump(B);
  console.log('A sent', JSON.stringify(da2.tx)); console.log('B sent', JSON.stringify(db2.tx));
  check(okA2, `A logged ${STD} as the answering station`); check(okB2, `B logged ${NONSTD} as the CQ station`);
  check(db2.tx.includes(`${NONSTD} <${STD}> RR73`), 'B acked in the Type 4 form');

  // ---- two standard calls: the everyday QSO, which must stay plain
  await A.waitForFunction(() => window.digiQsoState === 'IDLE' && !window.digiTxActive, null, { timeout: 20000 }).catch(() => {});
  await B.waitForFunction(() => window.digiQsoState === 'IDLE' && !window.digiTxActive, null, { timeout: 20000 }).catch(() => {});
  await reset(A); await reset(B);
  await A.evaluate(() => window.App.callsign.set('K1ABC'));
  await B.evaluate(() => window.digiCallCq());
  await pick(A, 'A', new RegExp(`^CQ ${STD} FN35$`));
  check(await A.evaluate(() => window.digiTxQueued) === `${STD} K1ABC JJ00`, 'A answers in the plain form');
  await pick(B, 'B', new RegExp(`^${STD} K1ABC JJ00$`));
  const okA3 = await logged(A, STD), okB3 = await logged(B, 'K1ABC');
  const da3 = await dump(A), db3 = await dump(B);
  console.log('A sent', JSON.stringify(da3.tx)); console.log('B sent', JSON.stringify(db3.tx));
  check(okA3, `A logged ${STD} with two standard calls`); check(okB3, 'B logged K1ABC with two standard calls');
  check(!da3.tx.concat(db3.tx).some(m => m.includes('<')), 'nothing was hashed');
} finally {
  await browser.close();
  if (!onServer) { server.close(); fs.rmSync(dist, { recursive: true, force: true }); }
}
console.log(fails ? `${fails} failed` : 'all passed');
process.exit(fails ? 1 : 0);

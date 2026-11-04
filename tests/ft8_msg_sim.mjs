// FT8 / FT4 message forms (#153), under node with the real ft8ts codec: what
// digi-msg.js builds must reach the other station whole, whatever the calls.
// A nonstandard call in a plain "HIS MY" message used to fall back to 13
// characters of free text, "AO40EUDXF VA2". Run through tests/test_ft8_msg.py.

import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath, pathToFileURL } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const d = createRequire(import.meta.url)(path.join(ROOT, 'resources/web-shared/digi-msg.js'));
const { encodeFT8, decodeFT8, encodeFT4, decodeFT4, HashCallBook } =
  await import(pathToFileURL(path.join(ROOT, 'resources/ft8ts/dist/ft8ts.mjs')));

let fails = 0;
function check(ok, msg) { console.log((ok ? 'PASS ' : 'FAIL ') + msg); if (!ok) fails++; }
function same(got, want, msg) { check(got === want, `${msg}: ${JSON.stringify(got)}` + (got === want ? '' : ` want ${JSON.stringify(want)}`)); }

function air(text, book, ft4) {
  const samples = (ft4 ? encodeFT4 : encodeFT8)(text, { sampleRate: 12000, baseFrequency: 1500 });
  const rx = new Float32Array(ft4 ? 90000 : 180000);
  rx.set(samples.slice(0, rx.length), ft4 ? 1200 : 6000);
  const out = (ft4 ? decodeFT4 : decodeFT8)(rx, { sampleRate: 12000, freqLow: 300, freqHigh: 2800, hashCallBook: book });
  return out.length ? out[0].msg : '';
}
// What the QSO logic of the station at the other end reads, its own and its
// DX's hashes known like the page's worker keeps them (digiHashCalls).
function heard(text, myCall, dxCall, ft4) {
  const book = new HashCallBook();
  book.save(myCall); book.save(dxCall);
  return d.digiStdMsg(air(text, book, ft4), myCall);
}

// ---- stdCall follows WSJT-X
for (const c of ['K1ABC', 'W1AW', 'VA2BBW', 'G4ABC', '9A1AA', 'E51AB', 'J5A', 'W1AW/P', 'W1AW/R', 'K1A']) check(d.digiStdCall(c), `${c} is standard`);
for (const c of ['AO40EUDXF', 'VE3/W9XYZ', '6W1/F4ABC', 'K1ABC/QRP', 'K1ABC/7', '3DA0XY', 'TM50IARU', 'YW100SC']) check(!d.digiStdCall(c), `${c} is nonstandard`);

// ---- the reported failure, and the forms WSJT-X sends
const q = d.digiQsoTexts('AO40EUDXF', 'VA2BBW', 'FN35', '-10');
same(q.call, '<AO40EUDXF> VA2BBW FN35', 'calling a nonstandard DX');
same(q.report, '<AO40EUDXF> VA2BBW -10', 'report');
same(q.rreport, '<AO40EUDXF> VA2BBW R-10', 'R-report');
same(q.rr73, 'AO40EUDXF <VA2BBW> RR73', 'RR73');
same(q.s73, 'AO40EUDXF <VA2BBW> 73', '73');
const r = d.digiQsoTexts('VA2BBW', 'AO40EUDXF', 'FN35', '-10');
same(r.call, '<VA2BBW> AO40EUDXF', 'a nonstandard call answering sends no grid');
same(r.report, 'VA2BBW <AO40EUDXF> -10', 'report from a nonstandard call');
same(r.rreport, 'VA2BBW <AO40EUDXF> R-10', 'R-report from a nonstandard call');
same(r.rr73, '<VA2BBW> AO40EUDXF RR73', 'RR73 from a nonstandard call');
same(r.s73, '<VA2BBW> AO40EUDXF 73', '73 from a nonstandard call');
same(d.digiQsoTexts('K1ABC', 'VA2BBW', 'FN35', '+3').rreport, 'K1ABC VA2BBW R+3', 'two standard calls stay plain');
same(d.digiCqText('VA2BBW', 'FN35'), 'CQ VA2BBW FN35', 'CQ with a grid');
same(d.digiCqText('AO40EUDXF', 'FN35'), 'CQ AO40EUDXF', 'a nonstandard CQ has no room for a grid');

// ---- over the air, both modes, every message of the QSO; each message must
// reach the other end as TO FROM DATA with nothing lost
const cases = [['AO40EUDXF', 'VA2BBW'], ['VA2BBW', 'AO40EUDXF'], ['6W1/F4ABC', 'VA2BBW'], ['K1ABC/QRP', 'W1AW'], ['K1ABC', 'VA2BBW']];
for (const ft4 of [false, true]) {
  const tag = ft4 ? 'FT4' : 'FT8';
  for (const [dx, my] of cases) {
    const t = d.digiQsoTexts(dx, my, 'FN35', '-10');
    const myStd = d.digiStdCall(my);
    same(heard(t.call, dx, my, ft4), `${dx} ${my}` + (myStd ? ' FN35' : ''), `${tag} ${dx}/${my} call`);
    same(heard(t.report, dx, my, ft4), `${dx} ${my} -10`, `${tag} ${dx}/${my} report`);
    same(heard(t.rreport, dx, my, ft4), `${dx} ${my} R-10`, `${tag} ${dx}/${my} R-report`);
    same(heard(t.rr73, dx, my, ft4), `${dx} ${my} RR73`, `${tag} ${dx}/${my} RR73`);
    same(heard(t.s73, dx, my, ft4), `${dx} ${my} 73`, `${tag} ${dx}/${my} 73`);
  }
  same(heard(d.digiCqText('AO40EUDXF', 'FN35'), 'VA2BBW', '', ft4), 'CQ AO40EUDXF', `${tag} nonstandard CQ`);
}
// A station nobody hashed yet learns the call from its CQ.
{
  const book = new HashCallBook();
  air('CQ AO40EUDXF', book);
  same(air('VA2BBW <AO40EUDXF> -10', book), 'VA2BBW <AO40EUDXF> -10', 'a nonstandard CQ teaches the hash book');
}

// ---- reading decodes
same(d.digiStripHash('VA2BBW <AO40EUDXF> -10'), 'VA2BBW AO40EUDXF -10', 'brackets come off');
same(d.digiStripHash('VA2BBW <...> -10'), 'VA2BBW <...> -10', 'an unresolved hash stays');
same(d.digiSenderCall('VA2BBW <AO40EUDXF> -10'), 'AO40EUDXF', 'sender of a hashed report');
same(d.digiSenderCall('<VA2BBW> AO40EUDXF RR73'), 'AO40EUDXF', 'sender of a hashed RR73');
same(d.digiSenderCall('VA2BBW <...> -10'), '', 'no sender for an unresolved hash');
same(d.digiSenderCall('CQ AO40EUDXF'), 'AO40EUDXF', 'sender of a nonstandard CQ');
same(d.digiSenderCall('CQ DX K1ABC FN42'), 'K1ABC', 'sender of a directed CQ');
same(d.digiSenderCall('CQ K1ABC FN42'), 'K1ABC', 'sender of a plain CQ');
same(d.digiSenderCall('CQ 145 K1ABC FN42'), 'K1ABC', 'a frequency in the CQ is no call');
same(d.digiStdMsg('K1ABC RR73; VA2BBW <AO40EUDXF> -08', 'VA2BBW'), 'VA2BBW AO40EUDXF -08', 'our half of a multi-answer');
same(d.digiStdMsg('K1ABC RR73; VA2BBW <AO40EUDXF> -08', 'K1ABC'), 'K1ABC AO40EUDXF RR73', 'the other half of a multi-answer');
same(d.digiStdMsg('K1ABC RR73; VA2BBW <...> -08', 'VA2BBW'), 'VA2BBW <...> -08', 'an unresolved multi-answer keeps <...>');

console.log(fails ? `${fails} failed` : 'all passed');
process.exit(fails ? 1 : 0);

// Needle meter — the analog-style multi-scale face Icom draws on the IC-7610
// and its relatives: S / Id / Po / SWR / COMP / ALC / Vd arcs under one needle.
//
// Loaded as a classic <script> by both the server build
// (resources/web/index.html via web.qrc alias) and the standalone build
// (resources/web-standalone/index.html via tools/build-static.sh). The host
// page keeps its tile-bar meter and hands the canvas to this file instead
// when the needle face is selected; the recorder copies that same canvas, so
// a video shows whichever face is on the page.
//
// Provides (on window.NeedleMeter):
//   isNeedleRig(model)   radios whose own screen shows this face
//   ratedPower(poCal)    watts at 100 % of the Po scale, from the rig's table
//   draw(canvas, st)     st = { tx, kind, s, value, poFull }
//   stop()               the host went back to the bar face
//   ASPECT               width / height of the face
//
// The face is drawn from geometry measured off the radio's screen, in a
// 611 x 256 design box. Two centres matter: the scale lines are concentric
// arcs about a point far below the box, while the needle swings about a much
// closer pivot — which is what makes the scales fan out the way they do.
// Every tick lies on a needle ray, so a position on any scale is simply the
// needle angle (degrees from vertical, -45 at rest, S9 at 0, +45 full scale).

(function () {
'use strict';

var W = 611, H = 256;
var CX = 319, CY = 650;          // centre of the scale arcs
var PX = 319, PY = 315;          // needle pivot
var D  = CY - PY;

var WHITE = '#f2f2f2', RED = '#e8392c', BLUE = '#2f7fe8', DIM = '#8d8d8d';
var FONT = 'system-ui, -apple-system, "Segoe UI", Roboto, sans-serif';

// Scale line radii (about the arc centre).
var R_S = 598, R_ID = 585, R_PO = 552, R_SWR = 513, R_SWR2 = 503,
    R_COMP = 458, R_LOW = 449;
var R_TIP = 612, R_TAIL = 437;   // the needle is the ray between these two

var NEEDLE_RIGS = ['IC-7610', 'IC-7600', 'IC-7760', 'IC-785x', 'IC-756 PRO',
                   'IC-756 PRO II', 'IC-756 PRO III', 'IC-R8600'];

// reading -> needle angle, read off the printed ticks.
var SCALES = {
    s:    [[-54, -45], [-48, -40.5], [-42, -36.2], [-36, -32.2], [-30, -27],
           [-24, -22.1], [-18, -16.9], [-12, -11.4], [-6, -5.8], [0, 0],
           [10, 7.8], [20, 15.4], [30, 23], [40, 30.6], [50, 38.1], [60, 45.4]],
    po:   [[0, -44], [10, -26.1], [20, -14.4], [30, -6.3], [40, 1.6], [50, 8.2],
           [60, 13.8], [70, 19.3], [80, 24.4], [90, 28.9], [100, 33.4],
           [110, 37.9], [120, 42]],
    swr:  [[1, -44], [1.1, -40], [1.2, -36.1], [1.3, -32.1], [1.4, -29.2],
           [1.5, -25.9], [2, -14.5], [3, 0], [6, 45]],
    comp: [[0, -46], [5, -34.6], [10, -21.4], [15, 3.2], [20, 22.1], [30, 45]],
    alc:  [[0, -45], [1, 1], [2, 45]],
    vd:   [[0, -45], [10, 10.5], [13.8, 22], [16, 34]],
    id:   [[0, -43.8], [2, -38.3], [4, -32.7], [6, -27.2], [8, -21.6],
           [10, -16], [12, -9.6], [14, -3.3], [16, 3.1], [18, 9.5], [20, 15.8],
           [22, 21.1], [24, 27.1], [26, 32.6], [28, 38.2], [30, 43.7]]
};
var LABELS = { po: 'Po', swr: 'SWR', alc: 'ALC', comp: 'COMP', vd: 'Vd', id: 'Id' };
var REST = -45, STOP_LO = -47, STOP_HI = 48;
var FINE = 0.42;        // face scale below which the small numbers are dropped

function angleOf(kind, v) {
    var t = SCALES[kind];
    if (!t || !isFinite(v)) return REST;
    if (v <= t[0][0]) return t[0][1];
    for (var i = 1; i < t.length; i++) {
        if (v <= t[i][0])
            return t[i - 1][1] + (t[i][1] - t[i - 1][1]) * (v - t[i - 1][0]) / (t[i][0] - t[i - 1][0]);
    }
    return t[t.length - 1][1];
}

function isNeedleRig(model) { return NEEDLE_RIGS.indexOf(model) !== -1; }

// The rig's Power table runs past the rated output (a 100 W radio's ends at
// 120, an IC-705's at 12), so the rating is the largest standard figure the
// table reaches.
function ratedPower(poCal) {
    var top = (poCal && poCal.length) ? poCal[poCal.length - 1][1] : 0;
    var ratings = [1000, 400, 200, 100, 50, 25, 10, 5];
    for (var i = 0; i < ratings.length; i++) if (top >= ratings[i]) return ratings[i];
    return 100;
}

// ---- Geometry ------------------------------------------------------------

// Where the needle ray at `theta` crosses the scale circle of radius R.
function pt(theta, R) {
    var a = theta * Math.PI / 180, s = Math.sin(a), c = Math.cos(a);
    var t = -D * c + Math.sqrt(R * R - D * D * s * s);
    return [PX + t * s, PY - t * c];
}

function arc(g, R, t0, t1, width, color) {
    var p0 = pt(t0, R), p1 = pt(t1, R);
    g.beginPath();
    g.arc(CX, CY, R, Math.atan2(p0[1] - CY, p0[0] - CX), Math.atan2(p1[1] - CY, p1[0] - CX));
    g.lineWidth = width; g.strokeStyle = color; g.lineCap = 'butt';
    g.stroke();
}

function tick(g, theta, R0, R1, width, color) {
    var a = pt(theta, R0), b = pt(theta, R1);
    g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]);
    g.lineWidth = width; g.strokeStyle = color; g.lineCap = 'butt';
    g.stroke();
}

function block(g, t0, t1, R0, R1, color) {
    var a = pt(t0, R0), b = pt(t1, R0), c = pt(t1, R1), d = pt(t0, R1);
    g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]);
    g.lineTo(c[0], c[1]); g.lineTo(d[0], d[1]); g.closePath();
    g.fillStyle = color; g.fill();
}

function roundRect(g, x, y, w, h, r) {
    g.beginPath();
    g.moveTo(x + r, y); g.lineTo(x + w - r, y); g.arcTo(x + w, y, x + w, y + r, r);
    g.lineTo(x + w, y + h - r); g.arcTo(x + w, y + h, x + w - r, y + h, r);
    g.lineTo(x + r, y + h); g.arcTo(x, y + h, x, y + h - r, r);
    g.lineTo(x, y + r); g.arcTo(x, y, x + r, y, r);
    g.closePath();
}

// ---- Static face ---------------------------------------------------------

// Text is sized in design units but never allowed under `minPx` CSS pixels:
// on a phone the face is a third of its design size, and the small scale
// numbers are dropped there instead (see FINE).
function text(g, f, str, x, y, px, color, weight) {
    var size = Math.max(px, f.minPx / f.scale);
    g.font = (weight || '600') + ' ' + size + 'px ' + FONT;
    g.fillStyle = color;
    g.fillText(str, x, y);
}

function drawFace(g, f) {
    var fine = f.scale >= FINE;
    var i, t;
    g.textAlign = 'center';
    g.textBaseline = 'middle';

    // S: white to S9, red beyond; a block for S0-S1 and for +60.
    arc(g, R_S, -54, 0.6, 6, WHITE);
    arc(g, R_S, 2.2, 52, 6, RED);
    block(g, -46.4, -40.5, R_S + 2, R_S + 13, WHITE);
    for (i = 2; i <= 9; i++) {
        t = SCALES.s[i][1];
        tick(g, t, R_S + 2, R_S + ((i === 5 || i === 9) ? 15 : 8), 3.8, WHITE);
    }
    for (i = 10; i <= 14; i++) {
        t = SCALES.s[i][1];
        tick(g, t, R_S + 2, R_S + (i % 2 ? 15 : 8), 3.8, RED);
    }
    block(g, 44.4, 46.4, R_S + 2, R_S + 15, RED);
    text(g, f, 'S', 30, 108, 30, WHITE, 'bold');
    text(g, f, '1', 115, 64, 27, WHITE);
    text(g, f, '5', 213, 37, 27, WHITE);
    text(g, f, '9', 320, 29, 27, WHITE);
    text(g, f, '+20', 392, 35, 25, RED);
    text(g, f, '+40', 466, 47, 25, RED);
    text(g, f, fine ? '+60dB' : '+60', fine ? 558 : 548, 73, 25, RED);

    // Id: the heavy line under the S scale, ticks every 2 A hanging from it.
    arc(g, R_ID, -54.8, 54.8, 9, WHITE);
    block(g, -45, -42.7, R_ID - 3, R_ID - 15, WHITE);
    block(g, 42.7, 44.8, R_ID - 3, R_ID - 15, WHITE);
    for (i = 1; i < SCALES.id.length - 1; i++)
        tick(g, SCALES.id[i][1], R_ID - 3, R_ID - (i % 5 ? 11 : 15), 3, WHITE);

    // Po (%).
    arc(g, R_PO, -54.8, 54.7, 6, WHITE);
    block(g, -45.1, -42.7, R_PO - 2, R_PO - 11, WHITE);
    for (i = 1; i < SCALES.po.length; i++) {
        var po = SCALES.po[i][0];
        tick(g, SCALES.po[i][1], R_PO - 2,
             R_PO - ((po === 10 || po === 20 || po === 50 || po === 100) ? 11 : 8), 3, WHITE);
    }

    // SWR: one line up to 3, a hollow band from there to infinity.
    arc(g, R_SWR, -55, 55, 6, WHITE);
    arc(g, R_SWR2, 0, 55, 5.5, WHITE);
    tick(g, 0.6, R_SWR + 2.5, R_SWR2 - 2.5, 4, WHITE);
    block(g, -44.6, -42.9, R_SWR - 2, R_SWR - 10, WHITE);
    for (i = 1; i <= 6; i++)
        tick(g, SCALES.swr[i][1], R_SWR - 2, R_SWR - (i >= 5 ? 10 : 7), 3, WHITE);

    // COMP (blue), with ALC (red) and Vd (white) sharing the arc under it.
    arc(g, R_COMP, -55, 55, 6, BLUE);
    block(g, -48, -44.1, R_COMP + 2, R_COMP + 12, BLUE);
    tick(g, -34.6, R_COMP + 2, R_COMP + 9, 2.6, BLUE);
    tick(g, -21.4, R_COMP + 2, R_COMP + 12, 3.6, BLUE);
    tick(g, 3.2, R_COMP + 2, R_COMP + 9, 2.6, BLUE);
    tick(g, 22.1, R_COMP + 2, R_COMP + 12, 3.6, BLUE);

    arc(g, R_LOW, -46, 2, 4.5, RED);
    block(g, -46.3, -41, R_LOW + 2, R_LOW - 9, RED);
    block(g, -1.6, 2, R_LOW + 2, R_LOW - 9, RED);

    arc(g, R_LOW, 10, 35.6, 4, WHITE);
    block(g, 10, 12.4, R_LOW + 2, R_LOW - 11, WHITE);
    tick(g, 22, R_LOW, R_LOW - 9, 2.6, WHITE);
    tick(g, 34.6, R_LOW + 2, R_LOW - 9, 2.6, WHITE);

    // Row names.
    text(g, f, 'Id', 72, 142, 17, WHITE);
    text(g, f, 'Po', 105, 163, 17, WHITE);
    text(g, f, 'SWR', 146, 190, 17, WHITE);
    text(g, f, 'COMP', 151, 210, 17, BLUE);
    text(g, f, 'ALC', 276, 224, 17, RED);

    if (fine) {
        text(g, f, '0', 133, 118, 17, WHITE);
        text(g, f, '10', 265, 88, 17, WHITE);
        text(g, f, '20', 402, 92, 17, WHITE);
        text(g, f, '30', 533, 127, 17, WHITE);
        text(g, f, 'A', 562, 137, 17, WHITE);

        text(g, f, '0', 157, 146, 17, WHITE);
        text(g, f, '10', 229, 129, 17, WHITE);
        text(g, f, '20', 271, 125, 17, WHITE);
        text(g, f, '50', 350, 122, 17, WHITE);
        text(g, f, '100', 448, 137, 17, WHITE);
        text(g, f, '%', 532, 160, 17, WHITE);

        text(g, f, '1', 185, 176, 17, WHITE);
        text(g, f, '1.5', 241, 166, 17, WHITE);
        text(g, f, '2', 287, 158, 17, WHITE);
        text(g, f, '3', 326, 159, 17, WHITE);
        text(g, f, '∞', 463, 181, 24, WHITE);

        text(g, f, '0', 200, 184, 17, BLUE);
        text(g, f, '10', 270, 174, 17, BLUE);
        text(g, f, '20', 372, 174, 17, BLUE);
        text(g, f, 'dB', 467, 203, 17, BLUE);

        text(g, f, '10', 340, 223, 16, WHITE);
        text(g, f, '16V', 388, 227, 16, WHITE);
        text(g, f, 'Vd', 430, 223, 16, WHITE);
    }

    // MET badge frame (its text is live) and the TX lamp.
    roundRect(g, 5.5, 3.5, 179, 31, 5);
    g.fillStyle = '#161616'; g.fill();
    g.save(); g.clip();
    g.fillStyle = '#5a5a5a'; g.fillRect(5, 3, 76, 32);
    g.restore();
    roundRect(g, 5.5, 3.5, 179, 31, 5);
    g.lineWidth = 1.5; g.strokeStyle = DIM; g.stroke();
    text(g, f, 'MET', 43, 20, 25, WHITE);

    if (f.tx) {
        roundRect(g, 6.5, 202.5, 86, 50, 6);
        g.fillStyle = '#2b0707'; g.fill();
        g.lineWidth = 2; g.strokeStyle = RED; g.stroke();
        text(g, f, 'TX', 49.5, 229, 40, RED, 'bold');
    }
}

// ---- Needle ballistics ---------------------------------------------------

// A damped spring: the pointer leans into a new reading and settles with a
// hint of overshoot, like a moving-coil meter. Readings only arrive about five
// times a second, so this is also what turns them into continuous motion.
var OMEGA = 14;         // natural frequency, rad/s
var ZETA  = 0.8;        // damping ratio (<1: slight overshoot)
var SETTLE_ANGLE = 0.02, SETTLE_SPEED = 0.2;

var cur = { canvas: null, st: null };
var pos = REST, vel = 0, target = REST;
var rafId = 0, lastTs = 0;
var face = null, faceKey = '';

function step(dt) {
    while (dt > 0) {
        var h = Math.min(dt, 0.004);
        vel += (OMEGA * OMEGA * (target - pos) - 2 * ZETA * OMEGA * vel) * h;
        pos += vel * h;
        if (pos < STOP_LO) { pos = STOP_LO; vel = 0; }
        if (pos > STOP_HI) { pos = STOP_HI; vel = 0; }
        dt -= h;
    }
}

function settled() {
    return Math.abs(target - pos) < SETTLE_ANGLE && Math.abs(vel) < SETTLE_SPEED;
}

function readout(st) {
    var v = st.value, k = st.kind;
    if (!isFinite(v)) return '';
    return k === 'po'   ? Math.round(v) + 'W'
         : k === 'swr'  ? v.toFixed(1)
         : k === 'comp' ? Math.round(v) + 'dB'
         : k === 'vd'   ? v.toFixed(1) + 'V'
         : k === 'id'   ? v.toFixed(1) + 'A'
         : v.toFixed(1);
}

function paint() {
    var canvas = cur.canvas, st = cur.st;
    if (!canvas || !st) return;
    var dpr = window.devicePixelRatio || 1;
    var cssW = canvas.clientWidth, cssH = canvas.clientHeight;
    if (!cssW || !cssH) return;
    var pw = Math.round(cssW * dpr), ph = Math.round(cssH * dpr);
    if (canvas.width !== pw || canvas.height !== ph) { canvas.width = pw; canvas.height = ph; }

    var scale = Math.min(cssW / W, cssH / H);
    var ox = (cssW - W * scale) / 2, oy = (cssH - H * scale) / 2;
    var f = { scale: scale, minPx: scale >= FINE ? 7 : 6, tx: !!st.tx };

    var key = pw + 'x' + ph + (f.tx ? 't' : 'r');
    if (key !== faceKey) {
        face = face || document.createElement('canvas');
        face.width = pw; face.height = ph;
        var fg = face.getContext('2d');
        fg.setTransform(dpr * scale, 0, 0, dpr * scale, dpr * ox, dpr * oy);
        drawFace(fg, f);
        faceKey = key;
    }

    var g = canvas.getContext('2d');
    g.setTransform(1, 0, 0, 1, 0, 0);
    g.clearRect(0, 0, pw, ph);
    g.drawImage(face, 0, 0);
    g.setTransform(dpr * scale, 0, 0, dpr * scale, dpr * ox, dpr * oy);

    // Badge: the armed reading, with its value while transmitting.
    var label = LABELS[st.kind] || 'Po';
    var str = st.tx ? label + ' ' + readout(st) : label;
    var px = Math.max(25, f.minPx / scale);
    g.textAlign = 'center'; g.textBaseline = 'middle';
    g.font = '600 ' + px + 'px ' + FONT;
    var tw = g.measureText(str).width;
    if (tw > 94) g.font = '600 ' + (px * 94 / tw) + 'px ' + FONT;
    g.fillStyle = WHITE;
    g.fillText(str, 132.5, 20);

    // Needle, over a dark edge so it still reads where it crosses a white arc.
    var a = pt(pos, R_TAIL), b = pt(pos, R_TIP);
    g.lineCap = 'butt';
    g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]);
    var nw = Math.max(3.6, 2 / scale);            // never under 2 CSS px
    g.lineWidth = nw + 3; g.strokeStyle = 'rgba(0,0,0,0.6)'; g.stroke();
    g.lineWidth = nw; g.strokeStyle = '#fafafa'; g.stroke();

    g.setTransform(1, 0, 0, 1, 0, 0);
}

function frame(ts) {
    rafId = 0;
    var dt = lastTs ? (ts - lastTs) / 1000 : 0.016;
    lastTs = ts;
    // After a stall (throttled tab) don't replay the gap; just catch up.
    if (dt > 0.25) { pos = target; vel = 0; } else step(dt);
    paint();
    if (settled()) { pos = target; vel = 0; lastTs = 0; paint(); return; }
    rafId = requestAnimationFrame(frame);
}

function draw(canvas, st) {
    if (cur.canvas !== canvas) faceKey = '';
    cur.canvas = canvas; cur.st = st;
    var t;
    if (!st.tx) t = angleOf('s', st.s);
    else if (st.kind === 'po') t = angleOf('po', st.value / (st.poFull || 100) * 100);
    else t = angleOf(SCALES[st.kind] ? st.kind : 'swr', st.value);
    target = Math.max(STOP_LO, Math.min(STOP_HI, t));

    // A hidden tab gets no animation frames; keep the canvas truthful for a
    // recording that is still running.
    if (document.hidden) { pos = target; vel = 0; paint(); return; }
    paint();
    if (!rafId && !settled()) rafId = requestAnimationFrame(frame);
}

function stop() {
    if (rafId) cancelAnimationFrame(rafId);
    rafId = 0; lastTs = 0;
    cur.canvas = null; cur.st = null;
    faceKey = '';
}

window.NeedleMeter = {
    ASPECT: W / H,
    isNeedleRig: isNeedleRig,
    ratedPower: ratedPower,
    draw: draw,
    stop: stop
};
})();

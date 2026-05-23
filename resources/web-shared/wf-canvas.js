// Shared waterfall renderer + circular sample buffer.
//
// Loaded as a classic <script> by both the server build
// (resources/web/index.html via web.qrc alias) and the standalone build
// (resources/web-standalone/index.html via tools/build-static.sh). All
// exports land on window so the existing call sites (which were inline
// before extraction) keep working unchanged.
//
// Provides:
//   constants  WF_FFT_SIZE, WF_DISPLAY_BINS, WF_FREQ_LOW, WF_FREQ_HIGH
//   state      wfBuf, wfBufPos, wfBufFull  (audio path writes the buffer)
//   render     drawWfCanvas(canvas, labelCanvas, opts)
//   helpers    wfFftInPlace, initWfColorTable
//
// Same algorithm both builds shipped before: Hann-windowed 4096-point
// FFT, linear-interpolated 1024-bin display, 4×-throttled scroll,
// per-slot auto-gain over a 60 dB range, slot-boundary marker lines.

var WF_FFT_SIZE     = 4096;
var WF_DISPLAY_BINS = 1024;   // covers 0-3000 Hz at 12 kHz / 4096
var WF_FREQ_LOW     = 300;    // display range start (Hz)
var WF_FREQ_HIGH    = 2800;   // display range end   (Hz)

var wfBuf      = new Float32Array(WF_FFT_SIZE);
var wfBufPos   = 0;
var wfBufFull  = false;

var wfColorTable     = null;     // Array[256] of [r,g,b]
var wfMinDb          = -100;
var wfMaxDb          = -40;
var wfSlotPeakDb     = -200;     // peak dB seen since the last slot boundary
var wfSlotCount      = 0;        // total slot boundaries seen — first few adapt faster
var wfScrollCounter  = 0;        // increments per call; scroll only every 4th
var wfSlotLines      = [];       // pixel rows currently marked as slot dividers
var wfLastSlotIndex  = -1;       // tracks slot transitions for boundary lines

var wfRe   = null;               // FFT real    scratch (WF_FFT_SIZE)
var wfIm   = null;               // FFT imag    scratch
var wfMags = null;               // dB magnitudes scratch (WF_DISPLAY_BINS)

function initWfColorTable() {
    wfColorTable = [];
    for (var i = 0; i < 256; i++) {
        var r, g, b;
        if (i < 85) {
            // black → dark blue (noise floor)
            r = 0; g = 0; b = Math.round(i * 3);
        } else if (i < 150) {
            // dark blue → bright blue (weak signals stay blue)
            var t = (i - 85) / 64;
            r = 0; g = Math.round(t * 60); b = 255;
        } else if (i < 190) {
            // bright blue → cyan
            var t2 = (i - 150) / 39;
            r = 0; g = 60 + Math.round(t2 * 195); b = 255;
        } else if (i < 225) {
            // cyan → yellow (medium signals)
            var t3 = (i - 190) / 34;
            r = Math.round(t3 * 255); g = 255; b = Math.round(255 * (1 - t3));
        } else {
            // yellow → red (strong signals)
            var t4 = (i - 225) / 30;
            r = 255; g = Math.round(255 * (1 - t4)); b = 0;
        }
        wfColorTable[i] = [r, g, b];
    }
}

function wfFftInPlace(re, im) {
    var n = re.length;
    var bits = Math.round(Math.log2(n));
    // Bit-reversal permutation
    for (var i = 0; i < n; i++) {
        var j = 0, x = i;
        for (var k = 0; k < bits; k++) { j = (j << 1) | (x & 1); x >>= 1; }
        if (j > i) {
            var t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    // Cooley-Tukey butterfly passes
    for (var len = 2; len <= n; len <<= 1) {
        var half = len >> 1;
        var ang = -2 * Math.PI / len;
        var wR = Math.cos(ang), wI = Math.sin(ang);
        for (var i2 = 0; i2 < n; i2 += len) {
            var curR = 1.0, curI = 0.0;
            for (var jj = 0; jj < half; jj++) {
                var uR = re[i2 + jj], uI = im[i2 + jj];
                var vR = curR * re[i2 + jj + half] - curI * im[i2 + jj + half];
                var vI = curR * im[i2 + jj + half] + curI * re[i2 + jj + half];
                re[i2 + jj]        = uR + vR;  im[i2 + jj]        = uI + vI;
                re[i2 + jj + half] = uR - vR;  im[i2 + jj + half] = uI - vI;
                var newCurR = curR * wR - curI * wI;
                curI = curR * wI + curI * wR;
                curR = newCurR;
            }
        }
    }
}

// Per-mode waterfall caller. opts:
//   rxEnabled    bool — when false, blank canvases + reset state
//   slotInfo     { slotIndex } — slot boundary detection + label TTL
//   labels       optional [{freq, from, cq, slotIndex}] — drawn on labelCanvas
//   centerOffset Hz offset added to each label.freq for X positioning
function drawWfCanvas(canvas, labelCanvas, opts) {
    if (!canvas) return;
    var bar = canvas.parentElement;
    if (!bar) return;
    var w = bar.clientWidth, h = bar.clientHeight;
    if (w <= 0 || h <= 0) return;

    if (canvas.width !== w || canvas.height !== h) {
        canvas.width = w;
        canvas.height = h;
        wfBufFull = false;
        wfSlotLines = [];
        wfLastSlotIndex = -1;
    }
    var ctx = canvas.getContext('2d');
    if (!wfColorTable) initWfColorTable();

    if (!opts.rxEnabled) {
        ctx.fillStyle = '#000';
        ctx.fillRect(0, 0, w, h);
        wfBufFull = false;
        wfSlotLines = [];
        wfLastSlotIndex = -1;
        if (labelCanvas) {
            var lctxClr = labelCanvas.getContext('2d');
            lctxClr.clearRect(0, 0, labelCanvas.width, labelCanvas.height);
        }
        return;
    }

    if (!wfBufFull) return;

    if (!wfRe) {
        wfRe   = new Float32Array(WF_FFT_SIZE);
        wfIm   = new Float32Array(WF_FFT_SIZE);
        wfMags = new Float32Array(WF_DISPLAY_BINS);
    }
    var re = wfRe, im = wfIm;
    for (var i = 0; i < WF_FFT_SIZE; i++) {
        var idx = (wfBufPos + i) % WF_FFT_SIZE;
        var wCoef = 0.5 * (1.0 - Math.cos(2 * Math.PI * i / (WF_FFT_SIZE - 1)));
        re[i] = wfBuf[idx] * wCoef;
        im[i] = 0;
    }
    wfFftInPlace(re, im);

    var mags = wfMags;
    var peakDb = -200;
    for (var b = 0; b < WF_DISPLAY_BINS; b++) {
        var mag = Math.sqrt(re[b] * re[b] + im[b] * im[b]) / WF_FFT_SIZE;
        var db = (mag > 1e-12) ? (20 * Math.log10(mag)) : -200;
        mags[b] = db;
        if (db > peakDb) peakDb = db;
    }
    if (peakDb > wfSlotPeakDb) wfSlotPeakDb = peakDb;

    wfScrollCounter++;
    if (wfScrollCounter < 4) return;
    wfScrollCounter = 0;

    var info = opts.slotInfo;
    var isSlotBoundary = false;
    if (wfLastSlotIndex < 0) wfLastSlotIndex = info.slotIndex;
    if (info.slotIndex !== wfLastSlotIndex) {
        wfLastSlotIndex = info.slotIndex;
        isSlotBoundary = true;
    }

    var imgData = ctx.getImageData(0, 0, w, h);
    var data = imgData.data;
    data.copyWithin(0, w * 4);

    for (var si = wfSlotLines.length - 1; si >= 0; si--) {
        wfSlotLines[si]--;
        if (wfSlotLines[si] < 0) wfSlotLines.splice(si, 1);
    }
    if (isSlotBoundary) {
        wfSlotLines.push(h - 1);
        var alpha = wfSlotCount < 3 ? 0.5 : 0.15;
        wfMaxDb = wfMaxDb * (1 - alpha) + wfSlotPeakDb * alpha;
        wfMinDb = wfMaxDb - 60;
        wfSlotPeakDb = -200;
        wfSlotCount++;
    }

    var binLow  = WF_FREQ_LOW  * WF_FFT_SIZE / 12000;
    var binHigh = WF_FREQ_HIGH * WF_FFT_SIZE / 12000;
    var rowStart = (h - 1) * w * 4;
    var dbRange = wfMaxDb - wfMinDb;
    if (dbRange < 1) dbRange = 1;
    for (var x = 0; x < w; x++) {
        var binExact = binLow + x * (binHigh - binLow) / w;
        var bin0 = Math.floor(binExact);
        var bin1 = bin0 + 1 < WF_FFT_SIZE / 2 ? bin0 + 1 : bin0;
        var frac = binExact - bin0;
        var dbi  = mags[bin0] * (1 - frac) + mags[bin1] * frac;
        var norm = (dbi - wfMinDb) / dbRange;
        if (norm < 0) norm = 0;
        if (norm > 1) norm = 1;
        var ci  = Math.round(norm * 255);
        var col = wfColorTable[ci];
        data[rowStart + x * 4 + 0] = col[0];
        data[rowStart + x * 4 + 1] = col[1];
        data[rowStart + x * 4 + 2] = col[2];
        data[rowStart + x * 4 + 3] = 255;
    }

    for (var si2 = 0; si2 < wfSlotLines.length; si2++) {
        var sy = wfSlotLines[si2];
        if (sy < 0 || sy >= h) continue;
        var sRowStart = sy * w * 4;
        for (var sx = 0; sx < w; sx++) {
            data[sRowStart + sx * 4 + 0] = 80;
            data[sRowStart + sx * 4 + 1] = 180;
            data[sRowStart + sx * 4 + 2] = 220;
            data[sRowStart + sx * 4 + 3] = 255;
        }
    }

    ctx.putImageData(imgData, 0, 0);

    if (labelCanvas && opts.labels) {
        if (labelCanvas.width !== w || labelCanvas.height !== h) {
            labelCanvas.width = w; labelCanvas.height = h;
        }
        var lctx = labelCanvas.getContext('2d');
        lctx.clearRect(0, 0, w, h);
        var currentSlot = info.slotIndex;
        var visibleLabels = opts.labels.filter(function (l) {
            return currentSlot - l.slotIndex < 5;
        });
        if (visibleLabels.length > 0) {
            var fontSize = 18;
            lctx.save();
            lctx.font = 'bold ' + fontSize + 'px "Courier New", monospace';
            lctx.textBaseline = 'top';
            lctx.lineJoin = 'round';
            for (var li = 0; li < visibleLabels.length; li++) {
                var lbl = visibleLabels[li];
                var lx = Math.round(
                    (lbl.freq + opts.centerOffset - WF_FREQ_LOW)
                    / (WF_FREQ_HIGH - WF_FREQ_LOW) * w);
                if (lx < 0 || lx >= w) continue;
                var fillColor   = lbl.cq ? '#40ff80' : '#40e0ff';
                var strokeColor = lbl.cq ? '#006020' : '#005060';
                var lineColor   = lbl.cq ? 'rgba(64,255,128,0.35)'
                                         : 'rgba(64,224,255,0.35)';

                var lineX = lx + 0.5;
                lctx.save();
                lctx.strokeStyle = lineColor;
                lctx.lineWidth = 2;
                lctx.beginPath();
                lctx.moveTo(lineX, 0);
                lctx.lineTo(lineX, h);
                lctx.stroke();
                lctx.restore();

                if (w >= 500) {
                    lctx.save();
                    lctx.lineWidth = 2;
                    var tw = lctx.measureText(lbl.from).width;
                    lctx.translate(lineX - Math.round(fontSize / 2),
                                   22 + tw + 4);
                    lctx.rotate(-Math.PI / 2);
                    lctx.fillStyle = 'rgba(0,0,0,0.75)';
                    lctx.fillRect(-2, -3, tw + 4, fontSize + 6);
                    lctx.strokeStyle = strokeColor;
                    lctx.strokeRect(-2, -3, tw + 4, fontSize + 6);
                    lctx.strokeStyle = strokeColor;
                    lctx.strokeText(lbl.from, 0, 0);
                    lctx.fillStyle = fillColor;
                    lctx.fillText(lbl.from, 0, 0);
                    lctx.restore();
                }
            }
            lctx.restore();
        }
    }
}

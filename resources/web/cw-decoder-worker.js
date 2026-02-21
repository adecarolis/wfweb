/// <reference lib="webworker" />
// CW Decoder Web Worker - Exact implementation from demo

importScripts('https://cdn.jsdelivr.net/npm/onnxruntime-web@1.20.0/dist/ort.min.js');

// Constants
const FFT_SIZE = 256;
const HOP_SIZE = 64;
const SAMPLE_RATE = 3200;

const ENGLISH_VOCABULARY = [
    "[UNK]", "/", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "?",
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    "\ue030", "\ue031", "\ue032", "\ue033", "\ue034", "\ue035", "\ue036", " "
];

const ABBREVIATION_MAP = {
    "\ue030": "AR",
    "\ue031": "BT",
    "\ue032": "HH",
    "\ue033": "KN",
    "\ue034": "SK",
    "\ue035": "BK",
    "\ue036": "UR"
};

const MODEL_URL = '/model.onnx';

let session = null;
let sessionLoading = false;
let sessionError = null;

// FFT
class FFT {
    constructor(size) {
        this.size = size;
        this.rev = new Uint32Array(size);
        this.sin = new Float32Array(size);
        this.cos = new Float32Array(size);

        let limit = 1;
        let bit = size >> 1;
        while (limit < size) {
            for (let i = 0; i < limit; i++) {
                this.rev[i + limit] = this.rev[i] + bit;
            }
            limit <<= 1;
            bit >>= 1;
        }

        for (let i = 0; i < size; i++) {
            const angle = (-2 * Math.PI * i) / size;
            this.sin[i] = Math.sin(angle);
            this.cos[i] = Math.cos(angle);
        }
    }

    transform(data) {
        const n = this.size;
        for (let i = 0; i < n; i++) {
            const j = this.rev[i];
            if (i < j) {
                [data[i*2], data[j*2]] = [data[j*2], data[i*2]];
                [data[i*2+1], data[j*2+1]] = [data[j*2+1], data[i*2+1]];
            }
        }

        for (let half = 1; half < n; half *= 2) {
            const step = half * 2;
            const angleStep = n / step;
            for (let i = 0; i < n; i += step) {
                for (let j = 0; j < half; j++) {
                    const ai = (i + j) * 2;
                    const bi = (i + j + half) * 2;
                    const aIdx = j * angleStep;
                    const wr = this.cos[aIdx];
                    const wi = this.sin[aIdx];

                    const tr = wr * data[bi] - wi * data[bi + 1];
                    const ti = wr * data[bi + 1] + wi * data[bi];

                    const ur = data[ai];
                    const ui = data[ai + 1];

                    data[ai] = ur + tr;
                    data[ai + 1] = ui + ti;
                    data[bi] = ur - tr;
                    data[bi + 1] = ui - ti;
                }
            }
        }
    }
}

class STFT {
    constructor(fftSize, hopSize) {
        this.fftSize = fftSize;
        this.hopSize = hopSize;
        this.fft = new FFT(fftSize);
        this.window = new Float32Array(fftSize);
        for (let i = 0; i < fftSize; i++) {
            this.window[i] = 0.5 * (1 - Math.cos((2 * Math.PI * i) / (fftSize - 1)));
        }
    }

    analyze(signal) {
        const result = [];
        const frame = new Float32Array(this.fftSize);
        const complex = new Float32Array(this.fftSize * 2);

        for (let i = 0; i + this.fftSize <= signal.length; i += this.hopSize) {
            frame.set(signal.subarray(i, i + this.fftSize));

            for (let j = 0; j < this.fftSize; j++) {
                frame[j] *= this.window[j];
            }

            for (let j = 0; j < this.fftSize; j++) {
                complex[j * 2] = frame[j];
                complex[j * 2 + 1] = 0;
            }

            this.fft.transform(complex);
            result.push(complex.slice());
        }

        return result;
    }
}

// Filter
function applyBandpassFilter(audio, sampleRate, centerFreq, bandwidth, passes = 4) {
    const q = centerFreq / bandwidth;
    const omega = (2 * Math.PI * centerFreq) / sampleRate;
    const sinOmega = Math.sin(omega);
    const cosOmega = Math.cos(omega);
    const alpha = sinOmega / (2 * q);

    const b0 = alpha;
    const b1 = 0;
    const b2 = -alpha;
    const a0 = 1 + alpha;
    const a1 = -2 * cosOmega;
    const a2 = 1 - alpha;

    const nb0 = b0 / a0;
    const nb1 = b1 / a0;
    const nb2 = b2 / a0;
    const na1 = a1 / a0;
    const na2 = a2 / a0;

    let input = audio;
    let output = new Float32Array(audio.length);

    for (let p = 0; p < passes; p++) {
        let x1 = 0, x2 = 0, y1 = 0, y2 = 0;

        for (let i = 0; i < input.length; i++) {
            const x0 = input[i];
            const y0 = nb0 * x0 + nb1 * x1 + nb2 * x2 - na1 * y1 - na2 * y2;
            output[i] = y0;
            x2 = x1;
            x1 = x0;
            y2 = y1;
            y1 = y0;
        }

        input = output;
        if (p < passes - 1) {
            output = new Float32Array(audio.length);
        }
    }

    return output;
}

// Spectrogram
function computeMagnitudeSpectrogram(complexSpec) {
    return complexSpec.map(frame => {
        const mags = new Float32Array(FFT_SIZE / 2 + 1);
        for (let j = 0; j < mags.length; j++) {
            const real = frame[j * 2];
            const imag = frame[j * 2 + 1];
            mags[j] = Math.sqrt(real * real + imag * imag);
        }
        return mags;
    });
}

function cropSpectrogram(spec) {
    const nBins = FFT_SIZE / 2 + 1;
    const cut = Math.floor(nBins / 4);
    return spec.map(frame => frame.slice(cut, nBins - cut));
}

function audioToSpectrogram(audio, filterFreq, filterWidth) {
    let processed = audio;
    if (filterFreq !== null && filterWidth > 0) {
        processed = applyBandpassFilter(audio, SAMPLE_RATE, filterFreq, filterWidth);
    }

    const stft = new STFT(FFT_SIZE, HOP_SIZE);
    const complex = stft.analyze(processed);
    const magnitude = computeMagnitudeSpectrogram(complex);
    return cropSpectrogram(magnitude);
}

// Text decoder - EXACT from demo
function replaceConsecutiveChars(str) {
    const regex = /(\S)\1+/g;
    return str.replace(regex, (match, p1) => p1 + ' '.repeat(match.length - 1));
}

function convertAbbreviationsWithSegments(str) {
    const abbrevs = Object.entries(ABBREVIATION_MAP);
    if (abbrevs.length === 0) return [{ text: str, isAbbreviation: false }];

    const pattern = abbrevs.map(([a]) => a).join('|');
    const regex = new RegExp(`(${pattern})`, 'g');

    const segments = [];
    let lastIndex = 0;
    let match;

    while ((match = regex.exec(str)) !== null) {
        if (match.index > lastIndex) {
            segments.push({ text: str.slice(lastIndex, match.index), isAbbreviation: false });
        }

        const entry = abbrevs.find(([a]) => a === match[0]);
        segments.push({ text: entry ? entry[1] : match[0], isAbbreviation: true });
        lastIndex = regex.lastIndex;
    }

    if (lastIndex < str.length) {
        segments.push({ text: str.slice(lastIndex), isAbbreviation: false });
    }

    return segments.length > 0 ? segments : [{ text: str, isAbbreviation: false }];
}

// KEY FUNCTION: Decode exactly like the demo
function decodePredictions(pred, predShape) {
    const [batchSize, timeSteps, numClasses] = predShape;
    const outputs = [];

    for (let i = 0; i < batchSize; i++) {
        const indices = [];

        for (let t = 0; t < timeSteps; t++) {
            let maxProb = -Infinity;
            let maxIdx = 0;
            const offset = i * timeSteps * numClasses + t * numClasses;

            for (let c = 0; c < numClasses; c++) {
                if (pred[offset + c] > maxProb) {
                    maxProb = pred[offset + c];
                    maxIdx = c;
                }
            }
            indices.push(maxIdx);
        }

        // Map to vocabulary and join (this creates the "fixed width" output)
        const resText = indices.map(c => ENGLISH_VOCABULARY[c] || '').join('');

        // Process
        const segments = convertAbbreviationsWithSegments(replaceConsecutiveChars(resText));
        outputs.push(segments);
    }

    return outputs;
}

// Model loading
async function ensureSession() {
    if (session) return session;
    if (sessionLoading) {
        while (sessionLoading) await new Promise(r => setTimeout(r, 10));
        if (sessionError) throw sessionError;
        return session;
    }

    sessionLoading = true;
    try {
        ort.env.wasm.wasmPaths = 'https://cdn.jsdelivr.net/npm/onnxruntime-web@1.20.0/dist/';
        session = await ort.InferenceSession.create(MODEL_URL, { executionProviders: ['wasm'] });
        console.log('[CW Decoder Worker] Model loaded');
        return session;
    } catch (err) {
        sessionError = err;
        throw err;
    } finally {
        sessionLoading = false;
    }
}

// Inference
async function handleRunInference(audioBuffer, filterFreq, filterWidth) {
    const sess = await ensureSession();

    const spec = audioToSpectrogram(audioBuffer, filterFreq, filterWidth);
    if (spec.length === 0) return [];

    const freqBins = spec[0].length;
    const flat = new Float32Array(spec.length * freqBins);
    for (let t = 0; t < spec.length; t++) {
        flat.set(spec[t], t * freqBins);
    }

    const input = new ort.Tensor('float32', flat, [1, spec.length, freqBins, 1]);
    const feeds = { [sess.inputNames[0]]: input };
    const results = await sess.run(feeds);
    const output = results[sess.outputNames[0]];

    const decoded = decodePredictions(output.data, output.dims);
    return decoded.length > 0 ? decoded[0] : [];
}

// Message handler
self.onmessage = async (e) => {
    const msg = e.data;

    try {
        if (msg.type === 'loadModel') {
            await ensureSession();
            self.postMessage({ type: 'modelLoaded' });
        } else if (msg.type === 'runInference') {
            const segments = await handleRunInference(
                msg.audioBuffer,
                msg.filterFreq,
                msg.filterWidth
            );
            self.postMessage({ type: 'inferenceResult', segments });
        } else {
            self.postMessage({ type: 'error', error: 'Unknown message type' });
        }
    } catch (err) {
        self.postMessage({ type: 'error', error: err.message });
    }
};

console.log('[CW Decoder Worker] Initialized');

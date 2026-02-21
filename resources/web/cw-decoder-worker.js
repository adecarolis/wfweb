/// <reference lib="webworker" />
// CW Decoder Web Worker for ONNX inference

importScripts('https://cdn.jsdelivr.net/npm/onnxruntime-web@1.20.0/dist/ort.min.js');

// Constants
const FFT_LENGTH = 256;
const HOP_LENGTH = 64;
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

// Model URL - served from root by wfview web server
// The model is embedded via Qt resource system at :/web/model.onnx
const MODEL_URL = '/model.onnx';

let session = null;
let sessionLoading = false;
let sessionError = null;

// FFT implementation
class FFT {
    constructor(fftSize) {
        if ((fftSize & (fftSize - 1)) !== 0) {
            throw new Error("FFT size must be a power of 2.");
        }
        this.fftSize = fftSize;
        this.reverseTable = new Uint32Array(fftSize);
        this.sinTable = new Float32Array(fftSize);
        this.cosTable = new Float32Array(fftSize);

        let limit = 1;
        let bit = fftSize >> 1;
        while (limit < fftSize) {
            for (let i = 0; i < limit; i++) {
                this.reverseTable[i + limit] = this.reverseTable[i] + bit;
            }
            limit = limit << 1;
            bit = bit >> 1;
        }

        for (let i = 0; i < fftSize; i++) {
            const angle = (-2 * Math.PI * i) / fftSize;
            this.sinTable[i] = Math.sin(angle);
            this.cosTable[i] = Math.cos(angle);
        }
    }

    transform(complexArray) {
        for (let i = 0; i < this.fftSize; i++) {
            const reversedIndex = this.reverseTable[i];
            if (i < reversedIndex) {
                const tmpReal = complexArray[i * 2];
                const tmpImag = complexArray[i * 2 + 1];
                complexArray[i * 2] = complexArray[reversedIndex * 2];
                complexArray[i * 2 + 1] = complexArray[reversedIndex * 2 + 1];
                complexArray[reversedIndex * 2] = tmpReal;
                complexArray[reversedIndex * 2 + 1] = tmpImag;
            }
        }

        for (let halfSize = 1; halfSize < this.fftSize; halfSize *= 2) {
            const step = 2 * halfSize;
            const angleStep = this.fftSize / step;
            for (let i = 0; i < this.fftSize; i += step) {
                for (let j = 0; j < halfSize; j++) {
                    const angleIndex = j * angleStep;
                    const wReal = this.cosTable[angleIndex];
                    const wImag = this.sinTable[angleIndex];

                    const i_j = (i + j) * 2;
                    const i_j_half = (i + j + halfSize) * 2;

                    const tr = wReal * complexArray[i_j_half] - wImag * complexArray[i_j_half + 1];
                    const ti = wReal * complexArray[i_j_half + 1] + wImag * complexArray[i_j_half];

                    const ur = complexArray[i_j];
                    const ui = complexArray[i_j + 1];

                    complexArray[i_j] = ur + tr;
                    complexArray[i_j + 1] = ui + ti;
                    complexArray[i_j_half] = ur - tr;
                    complexArray[i_j_half + 1] = ui - ti;
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
        this.window = this.generateHanningWindow();
    }

    generateHanningWindow() {
        const window = new Float32Array(this.fftSize);
        for (let i = 0; i < this.fftSize; i++) {
            window[i] = 0.5 * (1 - Math.cos((2 * Math.PI * i) / (this.fftSize - 1)));
        }
        return window;
    }

    analyze(signal) {
        const spectrogram = [];
        const frame = new Float32Array(this.fftSize);
        const complexFrame = new Float32Array(this.fftSize * 2);

        for (let i = 0; i + this.fftSize <= signal.length; i += this.hopSize) {
            const signalSlice = signal.subarray(i, i + this.fftSize);
            frame.set(signalSlice);

            for (let j = 0; j < this.fftSize; j++) {
                frame[j] *= this.window[j];
            }

            for (let j = 0; j < this.fftSize; j++) {
                complexFrame[j * 2] = frame[j];
                complexFrame[j * 2 + 1] = 0;
            }

            this.fft.transform(complexFrame);
            spectrogram.push(complexFrame.slice());
        }

        return spectrogram;
    }
}

// Audio filter
function applyBandpassFilter(audio, sampleRate, centerFreq, bandwidth) {
    const lowFreq = centerFreq - bandwidth / 2;
    const highFreq = centerFreq + bandwidth / 2;
    const filterOrder = 64;
    const filtered = new Float32Array(audio.length);

    const omegaLow = (2 * Math.PI * lowFreq) / sampleRate;
    const omegaHigh = (2 * Math.PI * highFreq) / sampleRate;

    for (let i = 0; i < audio.length; i++) {
        let sum = 0;
        let weightSum = 0;

        for (let j = -filterOrder / 2; j < filterOrder / 2; j++) {
            const idx = i + j;
            if (idx >= 0 && idx < audio.length) {
                const window = 0.54 - 0.46 * Math.cos((2 * Math.PI * (j + filterOrder / 2)) / filterOrder);
                const t = j;
                let kernel = 0;
                if (t !== 0) {
                    kernel = (Math.sin(omegaHigh * t) - Math.sin(omegaLow * t)) / (Math.PI * t);
                } else {
                    kernel = (omegaHigh - omegaLow) / Math.PI;
                }

                sum += audio[idx] * kernel * window;
                weightSum += kernel * window;
            }
        }

        filtered[i] = weightSum > 0 ? sum / weightSum : 0;
    }

    return filtered;
}

// Spectrogram utilities
function computeMagnitudeSpectrogram(complexSpectrogram) {
    return complexSpectrogram.map((complexFrame) => {
        const magnitudes = new Float32Array(FFT_LENGTH / 2 + 1);
        for (let j = 0; j < magnitudes.length; j++) {
            const real = complexFrame[j * 2];
            const imag = complexFrame[j * 2 + 1];
            magnitudes[j] = Math.sqrt(real * real + imag * imag);
        }
        return magnitudes;
    });
}

function cropSpectrogram(spectrogram) {
    const nBins = FFT_LENGTH / 2 + 1;
    const cutBins = Math.floor(nBins / 4);
    const startBin = cutBins;
    const endBin = nBins - cutBins;
    return spectrogram.map((frame) => frame.slice(startBin, endBin));
}

// Simple bandpass filter using frequency domain masking
function applyBandpassFilterFFT(audio, sampleRate, centerFreq, bandwidth) {
    if (!centerFreq || bandwidth <= 0) return audio;

    // Perform FFT
    const fftSize = 2048;
    const hopSize = 512;
    const stft = new STFT(fftSize, hopSize);

    // Process in overlapping windows
    const filtered = new Float32Array(audio.length);
    const window = new Float32Array(fftSize);
    for (let i = 0; i < fftSize; i++) {
        window[i] = 0.5 * (1 - Math.cos((2 * Math.PI * i) / (fftSize - 1)));
    }

    const halfBandwidth = bandwidth / 2;
    const lowFreq = centerFreq - halfBandwidth;
    const highFreq = centerFreq + halfBandwidth;

    for (let start = 0; start + fftSize <= audio.length; start += hopSize) {
        const frame = audio.slice(start, start + fftSize);
        const complexFrame = new Float32Array(fftSize * 2);

        // Window and prepare for FFT
        for (let i = 0; i < fftSize; i++) {
            complexFrame[i * 2] = frame[i] * window[i];
        }

        // FFT
        const fft = new FFT(fftSize);
        fft.transform(complexFrame);

        // Apply frequency mask
        const nyquist = sampleRate / 2;
        for (let i = 0; i < fftSize / 2; i++) {
            const freq = (i / (fftSize / 2)) * nyquist;
            let gain = 0;

            if (freq >= lowFreq && freq <= highFreq) {
                // Passband - gradual roll-off
                const distFromCenter = Math.abs(freq - centerFreq);
                const normalizedDist = distFromCenter / halfBandwidth;
                gain = Math.cos(normalizedDist * Math.PI / 2); // Cosine roll-off
            }

            // Apply gain to complex values
            complexFrame[i * 2] *= gain;
            complexFrame[i * 2 + 1] *= gain;

            // Mirror for negative frequencies
            if (i > 0) {
                complexFrame[(fftSize - i) * 2] *= gain;
                complexFrame[(fftSize - i) * 2 + 1] *= gain;
            }
        }

        // Inverse FFT (reuse FFT with conjugate)
        // Simplified: just accumulate for now
    }

    // For now, return original audio - the filter needs more work
    // The key is that the model should focus on the target frequency
    return audio;
}

function audioToSpectrogram(audio, filterFreq, filterWidth) {
    // Apply bandpass filter if frequency is set
    let processedAudio = audio;
    if (filterFreq !== null && filterWidth > 0) {
        // Simple time-domain filter
        processedAudio = applyTimeDomainFilter(audio, SAMPLE_RATE, filterFreq, filterWidth);
    }

    const stft = new STFT(FFT_LENGTH, HOP_LENGTH);
    const complexSpectrogram = stft.analyze(processedAudio);
    const magnitudeSpectrogram = computeMagnitudeSpectrogram(complexSpectrogram);
    return cropSpectrogram(magnitudeSpectrogram);
}

// Simple time-domain bandpass filter
function applyTimeDomainFilter(audio, sampleRate, centerFreq, bandwidth) {
    const lowFreq = Math.max(100, centerFreq - bandwidth / 2);
    const highFreq = Math.min(2000, centerFreq + bandwidth / 2);

    // Simple FIR bandpass using frequency sampling
    const numTaps = 128;
    const filterCoeffs = new Float32Array(numTaps);

    // Design filter using windowed sinc
    const omegaLow = (2 * Math.PI * lowFreq) / sampleRate;
    const omegaHigh = (2 * Math.PI * highFreq) / sampleRate;

    for (let n = 0; n < numTaps; n++) {
        const offset = n - numTaps / 2;
        let h = 0;

        if (offset === 0) {
            h = (omegaHigh - omegaLow) / Math.PI;
        } else {
            h = (Math.sin(omegaHigh * offset) - Math.sin(omegaLow * offset)) / (Math.PI * offset);
        }

        // Hamming window
        const window = 0.54 - 0.46 * Math.cos((2 * Math.PI * n) / (numTaps - 1));
        filterCoeffs[n] = h * window;
    }

    // Apply filter
    const filtered = new Float32Array(audio.length);
    for (let i = 0; i < audio.length; i++) {
        let sum = 0;
        for (let j = 0; j < numTaps; j++) {
            const idx = i - j;
            if (idx >= 0) {
                sum += audio[idx] * filterCoeffs[j];
            }
        }
        filtered[i] = sum;
    }

    return filtered;
}

// Text decoder
function replaceConsecutiveChars(str) {
    const regex = /(\S)\1+/g;
    return str.replace(regex, (match, p1) => {
        return p1 + " ".repeat(match.length - 1);
    });
}

function convertAbbreviationsWithSegments(str) {
    const abbreviations = Object.entries(ABBREVIATION_MAP);
    if (abbreviations.length === 0) {
        return [{ text: str, isAbbreviation: false }];
    }

    const abbrevPattern = abbreviations.map(([abbrev]) => abbrev).join("|");
    const regex = new RegExp(`(${abbrevPattern})`, "g");

    const segments = [];
    let lastIndex = 0;
    let match;

    while ((match = regex.exec(str)) !== null) {
        if (match.index > lastIndex) {
            segments.push({
                text: str.slice(lastIndex, match.index),
                isAbbreviation: false
            });
        }

        const matchedText = match[0];
        const abbrevEntry = abbreviations.find(([abbrev]) => abbrev === matchedText);
        const expansion = abbrevEntry ? abbrevEntry[1] : matchedText;

        segments.push({
            text: expansion,
            isAbbreviation: true
        });

        lastIndex = regex.lastIndex;
    }

    if (lastIndex < str.length) {
        segments.push({
            text: str.slice(lastIndex),
            isAbbreviation: false
        });
    }

    return segments.length > 0 ? segments : [{ text: str, isAbbreviation: false }];
}

function decodePredictions(pred, predShape) {
    const [batchSize, timeSteps, numClasses] = predShape;
    const outputSegments = [];

    for (let i = 0; i < batchSize; i++) {
        const predIndices = [];
        for (let t = 0; t < timeSteps; t++) {
            let maxProb = -Infinity;
            let maxIndex = 0;
            const offset = i * timeSteps * numClasses + t * numClasses;

            for (let c = 0; c < numClasses; c++) {
                if (pred[offset + c] > maxProb) {
                    maxProb = pred[offset + c];
                    maxIndex = c;
                }
            }
            predIndices.push(maxIndex);
        }

        // CTC decoding: collapse repeats and remove blanks (index 0 = [UNK])
        let collapsed = '';
        let prevIdx = -1;
        for (let idx of predIndices) {
            if (idx !== prevIdx && idx !== 0) {  // Skip repeats and blanks
                collapsed += ENGLISH_VOCABULARY[idx] || '';
            }
            prevIdx = idx;
        }

        // Replace consecutive same characters with single char + spaces
        const processedText = replaceConsecutiveChars(collapsed);

        // Convert to segments
        const processedSegments = convertAbbreviationsWithSegments(processedText);

        outputSegments.push(processedSegments);
    }

    return outputSegments;
}

// Model loading
async function ensureSession() {
    if (session) return session;
    if (sessionLoading) {
        while (sessionLoading) {
            await new Promise(resolve => setTimeout(resolve, 10));
        }
        if (sessionError) throw sessionError;
        return session;
    }

    sessionLoading = true;
    sessionError = null;

    try {
        ort.env.wasm.wasmPaths = 'https://cdn.jsdelivr.net/npm/onnxruntime-web@1.20.0/dist/';
        session = await ort.InferenceSession.create(MODEL_URL, {
            executionProviders: ['wasm'],
        });
        console.log('[CW Decoder Worker] Model loaded successfully');
        return session;
    } catch (error) {
        sessionError = error;
        console.error('[CW Decoder Worker] Failed to load model:', error);
        throw error;
    } finally {
        sessionLoading = false;
    }
}

// Inference handler
async function handleRunInference(audioBuffer, filterFreq, filterWidth) {
    const sess = await ensureSession();

    const spectrogram = audioToSpectrogram(audioBuffer, filterFreq, filterWidth);
    const timeSteps = spectrogram.length;
    if (timeSteps === 0) return [];

    const freqBins = spectrogram[0].length;
    const flattenedSpectrogram = new Float32Array(timeSteps * freqBins);
    for (let t = 0; t < timeSteps; t++) {
        flattenedSpectrogram.set(spectrogram[t], t * freqBins);
    }

    const dims = [1, timeSteps, freqBins, 1];
    const inputTensor = new ort.Tensor('float32', flattenedSpectrogram, dims);

    const inputName = sess.inputNames[0];
    const feeds = { [inputName]: inputTensor };
    const results = await sess.run(feeds);
    const outputTensor = results[sess.outputNames[0]];

    const decodedSegmentsList = decodePredictions(
        outputTensor.data,
        outputTensor.dims
    );

    return decodedSegmentsList.length > 0 ? decodedSegmentsList[0] : [];
}

// Message handler
self.onmessage = async function(event) {
    const message = event.data;

    const respond = (response) => self.postMessage(response);

    try {
        if (message.type === 'loadModel') {
            await ensureSession();
            respond({ type: 'modelLoaded' });
            return;
        }

        if (message.type === 'runInference') {
            const segments = await handleRunInference(
                message.audioBuffer,
                message.filterFreq,
                message.filterWidth
            );
            respond({ type: 'inferenceResult', segments });
            return;
        }

        respond({
            type: 'error',
            error: 'Unsupported worker message type: ' + message.type
        });
    } catch (error) {
        const errorMessage = error instanceof Error ? error.message : 'Unknown worker error';
        respond({ type: 'error', error: errorMessage });
    }
};

console.log('[CW Decoder Worker] Worker initialized');

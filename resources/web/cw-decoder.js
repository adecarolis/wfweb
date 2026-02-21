// CW Decoder Controller for wfview
// Integrates waterfall display, ONNX inference, and UI controls

(function() {
    'use strict';

    // Decoder state
    let decoderState = {
        enabled: false,
        loading: false,
        loaded: false,
        gain: 0,           // 0 or 20 dB
        filterWidth: 250,  // 100 or 250 Hz
        filterFreq: 600,   // Center frequency (null = disabled)
        currentSegments: [],
        isDecoding: false
    };

    // Worker
    let decoderWorker = null;

    // Waterfall rendering
    let waterfallCanvas = null;
    let waterfallCtx = null;
    let rafId = null;
    let colorLUT = null;
    let renderState = { lastTime: performance.now(), pixelAccumulator: 0 };

    // Audio analysis (simulated from incoming samples)
    let sampleBuffer = [];
    let frequencyData = null;
    let analyserBufferSize = 2048;

    // Constants
    const FFT_SIZE = 4096;
    const MIN_FREQ = 100;
    const MAX_FREQ = 1500;
    const BUFFER_DURATION_S = 12;
    const INFERENCE_INTERVAL_MS = 250;
    const SAMPLE_RATE = 3200;  // Target rate for inference

    // Audio buffer for inference (circular buffer)
    let inferenceBuffer = new Float32Array(BUFFER_DURATION_S * SAMPLE_RATE);
    let inferenceWritePos = 0;
    let inferenceIntervalId = null;
    let lastInferenceTime = 0;

    // Initialize decoder
    function initDecoder() {
        // Create color LUT
        colorLUT = buildColorLUT();

        // Create UI elements
        createDecoderUI();

        console.log('[CW Decoder] Initialized');
    }

    // Build color lookup table
    function buildColorLUT() {
        const lut = new Array(256);
        for (let v = 0; v < 256; v++) {
            let t = v / 255;
            const gamma = 2.2;
            t = Math.pow(t, gamma);
            const hue = (220 * (1 - t)) / 360;
            const sat = 1.0;
            const light = 0.15 + 0.75 * t;
            lut[v] = hslToRgb(hue, sat, light);
        }
        return lut;
    }

    function hslToRgb(h, s, l) {
        const hue2rgb = (p, q, t) => {
            if (t < 0) t += 1;
            if (t > 1) t -= 1;
            if (t < 1 / 6) return p + (q - p) * 6 * t;
            if (t < 1 / 2) return q;
            if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
            return p;
        };

        let r, g, b;
        if (s === 0) {
            r = g = b = l;
        } else {
            const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
            const p = 2 * l - q;
            r = hue2rgb(p, q, h + 1 / 3);
            g = hue2rgb(p, q, h);
            b = hue2rgb(p, q, h - 1 / 3);
        }

        return [Math.round(r * 255), Math.round(g * 255), Math.round(b * 255)];
    }

    // Simple FFT for waterfall display
    function computeFFT(samples) {
        const N = samples.length;
        if ((N & (N - 1)) !== 0) return null; // Must be power of 2

        // Create complex array
        const complex = new Float32Array(N * 2);
        for (let i = 0; i < N; i++) {
            complex[i * 2] = samples[i];
            complex[i * 2 + 1] = 0;
        }

        // Bit reversal
        const bits = Math.log2(N);
        for (let i = 0; i < N; i++) {
            let j = 0;
            for (let k = 0; k < bits; k++) {
                j = (j << 1) | ((i >> k) & 1);
            }
            if (j > i) {
                const tmpR = complex[i * 2];
                const tmpI = complex[i * 2 + 1];
                complex[i * 2] = complex[j * 2];
                complex[i * 2 + 1] = complex[j * 2 + 1];
                complex[j * 2] = tmpR;
                complex[j * 2 + 1] = tmpI;
            }
        }

        // FFT
        for (let size = 2; size <= N; size *= 2) {
            const halfsize = size / 2;
            const tablestep = N / size;
            for (let i = 0; i < N; i += size) {
                for (let j = i, k = 0; j < i + halfsize; j++, k += tablestep) {
                    const tpre =  complex[2 * (j + halfsize)] * Math.cos(2 * Math.PI * k / N) +
                                  complex[2 * (j + halfsize) + 1] * Math.sin(2 * Math.PI * k / N);
                    const tpim = -complex[2 * (j + halfsize)] * Math.sin(2 * Math.PI * k / N) +
                                  complex[2 * (j + halfsize) + 1] * Math.cos(2 * Math.PI * k / N);

                    complex[2 * (j + halfsize)] = complex[2 * j] - tpre;
                    complex[2 * (j + halfsize) + 1] = complex[2 * j + 1] - tpim;
                    complex[2 * j] += tpre;
                    complex[2 * j + 1] += tpim;
                }
            }
        }

        // Compute magnitude
        const magnitude = new Float32Array(N / 2);
        for (let i = 0; i < N / 2; i++) {
            const real = complex[i * 2];
            const imag = complex[i * 2 + 1];
            magnitude[i] = Math.sqrt(real * real + imag * imag);
        }

        return magnitude;
    }

    // Create decoder UI elements
    function createDecoderUI() {
        // Find CW bar
        const cwBar = document.getElementById('cwBar');
        if (!cwBar) {
            console.warn('[CW Decoder] CW bar not found, deferring UI creation');
            setTimeout(createDecoderUI, 500);
            return;
        }

        // Check if already created
        if (document.getElementById('cwDecoderSection')) return;

        // Insert decoder section after macro grids
        const macroGrid2 = document.getElementById('cwMacroGrid2');
        if (!macroGrid2) return;

        const decoderSection = document.createElement('div');
        decoderSection.id = 'cwDecoderSection';
        decoderSection.className = 'cw-decoder-section';
        decoderSection.innerHTML = `
            <div class="cw-decoder-controls">
                <button id="cwDecoderToggle" class="cw-decoder-btn">DECODE: OFF</button>
                <button id="cwDecoderGain" class="cw-decoder-btn">GAIN: 0dB</button>
                <button id="cwDecoderFilter" class="cw-decoder-btn">FIL: 250Hz</button>
            </div>
            <div class="cw-waterfall-container">
                <canvas id="cwWaterfallCanvas" class="cw-waterfall-canvas"></canvas>
                <div id="cwFilterBand" class="cw-filter-band"></div>
                <div class="cw-waterfall-hint">Click to set freq, scroll to adjust</div>
            </div>
            <div id="cwDecoderText" class="cw-decoder-text"></div>
        `;

        macroGrid2.parentNode.insertBefore(decoderSection, macroGrid2.nextSibling);

        // Setup canvas
        waterfallCanvas = document.getElementById('cwWaterfallCanvas');
        if (waterfallCanvas) {
            waterfallCtx = waterfallCanvas.getContext('2d');
            setupCanvasSizing();
        }

        // Add event listeners
        setupEventListeners();

        // Add CSS
        addDecoderStyles();
    }

    function setupCanvasSizing() {
        if (!waterfallCanvas) return;

        const container = waterfallCanvas.parentElement;
        if (!container) return;

        const rect = container.getBoundingClientRect();
        const dpr = window.devicePixelRatio || 1;

        waterfallCanvas.width = rect.clientWidth * dpr;
        waterfallCanvas.height = rect.clientHeight * dpr;

        // Initialize frequency data array
        frequencyData = new Uint8Array(FFT_SIZE / 2);
    }

    function setupEventListeners() {
        // Toggle button
        const toggleBtn = document.getElementById('cwDecoderToggle');
        if (toggleBtn) {
            toggleBtn.addEventListener('click', toggleDecoder);
        }

        // Gain button
        const gainBtn = document.getElementById('cwDecoderGain');
        if (gainBtn) {
            gainBtn.addEventListener('click', toggleGain);
        }

        // Filter button
        const filterBtn = document.getElementById('cwDecoderFilter');
        if (filterBtn) {
            filterBtn.addEventListener('click', toggleFilterWidth);
        }

        // Canvas interactions
        if (waterfallCanvas) {
            waterfallCanvas.addEventListener('click', handleCanvasClick);
            waterfallCanvas.addEventListener('wheel', handleCanvasWheel, { passive: false });
        }

        // Resize observer
        const resizeObserver = new ResizeObserver(() => {
            setupCanvasSizing();
        });
        if (waterfallCanvas && waterfallCanvas.parentElement) {
            resizeObserver.observe(waterfallCanvas.parentElement);
        }
    }

    function addDecoderStyles() {
        if (document.getElementById('cwDecoderStyles')) return;

        const style = document.createElement('style');
        style.id = 'cwDecoderStyles';
        style.textContent = `
            .cw-decoder-section {
                margin: 8px 0;
                border-top: 1px solid #0a0;
                padding-top: 8px;
            }
            .cw-decoder-controls {
                display: flex;
                gap: 8px;
                margin-bottom: 8px;
                justify-content: center;
            }
            .cw-decoder-btn {
                padding: 4px 12px;
                background: #001a00;
                border: 1px solid #0a0;
                color: #0a0;
                font-size: 11px;
                font-weight: bold;
                cursor: pointer;
                font-family: monospace;
            }
            .cw-decoder-btn:hover {
                background: #0a0;
                color: #000;
            }
            .cw-decoder-btn.active {
                background: #0a0;
                color: #000;
            }
            .cw-decoder-btn.loading {
                background: #1a1a00;
                border-color: #aa0;
                color: #aa0;
            }
            .cw-waterfall-container {
                position: relative;
                width: 100%;
                height: 128px;
                background: #000;
                border: 1px solid #0a0;
                border-radius: 4px;
                overflow: hidden;
            }
            .cw-waterfall-canvas {
                display: block;
                width: 100%;
                height: 100%;
            }
            .cw-filter-band {
                position: absolute;
                left: 0;
                right: 0;
                pointer-events: none;
                border-top: 1px solid #f00;
                border-bottom: 1px solid #f00;
                display: none;
            }
            .cw-filter-band.active {
                display: block;
            }
            .cw-waterfall-hint {
                position: absolute;
                bottom: 4px;
                left: 50%;
                transform: translateX(-50%);
                font-size: 10px;
                color: #0a0;
                background: rgba(0, 0, 0, 0.8);
                padding: 2px 8px;
                border-radius: 2px;
                pointer-events: none;
            }
            .cw-decoder-text {
                background: #000;
                border: 1px solid #0a0;
                border-radius: 4px;
                padding: 8px;
                margin-top: 8px;
                min-height: 32px;
                font-family: 'Courier New', monospace;
                font-size: 18px;
                color: #0f0;
                white-space: pre-wrap;
                display: flex;
                flex-wrap: wrap;
                gap: 0;
            }
            .cw-decoder-char {
                display: inline-block;
            }
            .cw-decoder-char.abbrev {
                color: #ff0;
                font-weight: bold;
            }
        `;
        document.head.appendChild(style);
    }

    // Toggle decoder on/off
    async function toggleDecoder() {
        if (decoderState.loading) return;

        if (decoderState.enabled) {
            stopDecoder();
        } else {
            await startDecoder();
        }
    }

    async function startDecoder() {
        if (decoderState.enabled) return;

        decoderState.loading = true;
        updateButtonStates();

        try {
            // Initialize worker
            decoderWorker = new Worker('cw-decoder-worker.js');
            decoderWorker.onmessage = handleWorkerMessage;

            // Load model
            decoderWorker.postMessage({ type: 'loadModel' });

            // Wait for model to load (with timeout)
            await new Promise((resolve, reject) => {
                const timeout = setTimeout(() => reject(new Error('Model load timeout')), 30000);

                const checkLoaded = setInterval(() => {
                    if (decoderState.loaded) {
                        clearInterval(checkLoaded);
                        clearTimeout(timeout);
                        resolve();
                    }
                }, 100);
            });

            // Start waterfall rendering
            startWaterfall();

            // Start inference loop
            lastInferenceTime = performance.now();

            decoderState.enabled = true;
            decoderState.isDecoding = true;
            console.log('[CW Decoder] Started');

        } catch (error) {
            console.error('[CW Decoder] Failed to start:', error);
            stopDecoder();
        } finally {
            decoderState.loading = false;
            updateButtonStates();
            updateFilterBand();
        }
    }

    function stopDecoder() {
        // Stop waterfall
        if (rafId) {
            cancelAnimationFrame(rafId);
            rafId = null;
        }

        // Terminate worker
        if (decoderWorker) {
            decoderWorker.terminate();
            decoderWorker = null;
        }

        decoderState.enabled = false;
        decoderState.isDecoding = false;
        decoderState.loaded = false;
        decoderState.loading = false;

        // Clear display
        clearDecoderText();

        console.log('[CW Decoder] Stopped');
        updateButtonStates();
    }

    function handleWorkerMessage(event) {
        const message = event.data;

        switch (message.type) {
            case 'modelLoaded':
                decoderState.loaded = true;
                decoderState.loading = false;
                updateButtonStates();
                break;

            case 'inferenceResult':
                decoderState.currentSegments = message.segments || [];
                updateDecoderText();
                break;

            case 'error':
                console.error('[CW Decoder Worker] Error:', message.error);
                break;
        }
    }

    // Toggle gain 0/20 dB
    function toggleGain() {
        decoderState.gain = decoderState.gain === 0 ? 20 : 0;
        updateButtonStates();
    }

    // Toggle filter width 100/250 Hz
    function toggleFilterWidth() {
        decoderState.filterWidth = decoderState.filterWidth === 100 ? 250 : 100;
        updateButtonStates();
        updateFilterBand();
    }

    // Update button states
    function updateButtonStates() {
        const toggleBtn = document.getElementById('cwDecoderToggle');
        const gainBtn = document.getElementById('cwDecoderGain');
        const filterBtn = document.getElementById('cwDecoderFilter');

        if (toggleBtn) {
            if (decoderState.loading) {
                toggleBtn.textContent = 'LOADING...';
                toggleBtn.classList.add('loading');
            } else if (decoderState.enabled) {
                toggleBtn.textContent = 'DECODE: ON';
                toggleBtn.classList.add('active');
                toggleBtn.classList.remove('loading');
            } else {
                toggleBtn.textContent = 'DECODE: OFF';
                toggleBtn.classList.remove('active', 'loading');
            }
        }

        if (gainBtn) {
            gainBtn.textContent = 'GAIN: ' + decoderState.gain + 'dB';
            if (decoderState.gain === 20) {
                gainBtn.classList.add('active');
            } else {
                gainBtn.classList.remove('active');
            }
        }

        if (filterBtn) {
            filterBtn.textContent = 'FIL: ' + decoderState.filterWidth + 'Hz';
        }
    }

    // Waterfall rendering
    function startWaterfall() {
        if (!waterfallCanvas) return;

        frequencyData = new Uint8Array(FFT_SIZE / 2);
        renderState.lastTime = performance.now();
        renderState.pixelAccumulator = 0;

        // Clear canvas
        waterfallCtx.fillStyle = '#000';
        waterfallCtx.fillRect(0, 0, waterfallCanvas.width, waterfallCanvas.height);

        renderWaterfall();
    }

    function renderWaterfall() {
        if (!decoderState.enabled || !waterfallCanvas) return;

        const now = performance.now();
        const dt = (now - renderState.lastTime) / 1000;
        renderState.lastTime = now;

        const canvas = waterfallCanvas;
        const ctx = waterfallCtx;
        const durationSeconds = BUFFER_DURATION_S;
        const pxPerSec = canvas.width / durationSeconds;
        renderState.pixelAccumulator += dt * pxPerSec;

        let step = Math.floor(renderState.pixelAccumulator);
        if (step <= 0) {
            rafId = requestAnimationFrame(renderWaterfall);
            return;
        }
        renderState.pixelAccumulator -= step;
        if (step > canvas.width) step = canvas.width;

        // Get frequency data from sample buffer
        updateFrequencyData();

        // Scroll left
        ctx.drawImage(
            canvas,
            0, 0, canvas.width, canvas.height,
            -step, 0, canvas.width, canvas.height
        );

        // Draw new column
        const column = ctx.createImageData(1, canvas.height);
        const buf = column.data;

        for (let y = 0; y < canvas.height; y++) {
            const invY = canvas.height - 1 - y;
            const freqRatio = invY / Math.max(1, canvas.height - 1);
            const binIdx = Math.floor(freqRatio * (FFT_SIZE / 2));
            const v = frequencyData[Math.min(binIdx, frequencyData.length - 1)] || 0;
            const [r, g, b] = colorLUT[v];
            const p = y * 4;
            buf[p] = r;
            buf[p + 1] = g;
            buf[p + 2] = b;
            buf[p + 3] = 255;
        }

        for (let i = 0; i < step; i++) {
            ctx.putImageData(column, canvas.width - step + i, 0);
        }

        // Trigger inference periodically
        if (now - lastInferenceTime > INFERENCE_INTERVAL_MS) {
            runInference();
            lastInferenceTime = now;
        }

        rafId = requestAnimationFrame(renderWaterfall);
    }

    function updateFrequencyData() {
        // If we have samples in the buffer, compute FFT
        if (sampleBuffer.length >= FFT_SIZE) {
            const samples = sampleBuffer.slice(-FFT_SIZE);
            const magnitude = computeFFT(samples);
            if (magnitude) {
                // Convert to dB and normalize to 0-255
                for (let i = 0; i < frequencyData.length && i < magnitude.length; i++) {
                    const db = 20 * Math.log10(magnitude[i] + 1e-10);
                    // Normalize: -70 to -30 dB range
                    const normalized = Math.max(0, Math.min(255, (db + 70) / 40 * 255));
                    frequencyData[i] = Math.floor(normalized);
                }
            }
        }
    }

    // Filter band overlay
    function updateFilterBand() {
        const band = document.getElementById('cwFilterBand');
        if (!band) return;

        if (decoderState.filterFreq === null) {
            band.classList.remove('active');
            return;
        }

        const range = MAX_FREQ - MIN_FREQ;
        const half = decoderState.filterWidth / 2;
        const lower = Math.max(MIN_FREQ, decoderState.filterFreq - half);
        const upper = Math.min(MAX_FREQ, decoderState.filterFreq + half);

        const topPercent = ((MAX_FREQ - upper) / range) * 100;
        const heightPercent = ((upper - lower) / range) * 100;

        band.style.top = topPercent + '%';
        band.style.height = heightPercent + '%';
        band.classList.add('active');
    }

    // Canvas interactions
    function handleCanvasClick(event) {
        const rect = waterfallCanvas.getBoundingClientRect();
        const y = event.clientY - rect.top;

        if (decoderState.filterFreq !== null) {
            // Disable filter
            decoderState.filterFreq = null;
        } else {
            // Set filter frequency
            const canvasHeight = rect.height;
            const invY = canvasHeight - y;
            const freqRange = MAX_FREQ - MIN_FREQ;
            const rawFreq = MIN_FREQ + (invY / canvasHeight) * freqRange;

            // Constrain to decodable range
            const halfWidth = decoderState.filterWidth / 2;
            let freq = rawFreq;
            if (freq - halfWidth < 400) freq = 400 + halfWidth;
            if (freq + halfWidth > 1200) freq = 1200 - halfWidth;

            decoderState.filterFreq = Math.round(freq);
        }

        updateFilterBand();
    }

    function handleCanvasWheel(event) {
        event.preventDefault();

        if (decoderState.filterFreq === null) return;

        const step = 20;
        let newFreq = decoderState.filterFreq;

        if (event.deltaY < 0) {
            newFreq += step;
        } else if (event.deltaY > 0) {
            newFreq -= step;
        }

        // Constrain
        const halfWidth = decoderState.filterWidth / 2;
        if (newFreq - halfWidth < 400) newFreq = 400 + halfWidth;
        if (newFreq + halfWidth > 1200) newFreq = 1200 - halfWidth;

        decoderState.filterFreq = Math.round(newFreq);
        updateFilterBand();
    }

    // Inference
    function runInference() {
        if (!decoderState.enabled || !decoderWorker || !decoderState.loaded) return;

        // Get last 12 seconds from inference buffer
        const buffer = new Float32Array(inferenceBuffer.length);

        // Copy circular buffer in order
        for (let i = 0; i < inferenceBuffer.length; i++) {
            const idx = (inferenceWritePos + i) % inferenceBuffer.length;
            buffer[i] = inferenceBuffer[idx];
        }

        decoderWorker.postMessage({
            type: 'runInference',
            audioBuffer: buffer,
            filterFreq: decoderState.filterFreq,
            filterWidth: decoderState.filterWidth
        });
    }

    // Update decoder text display
    function updateDecoderText() {
        const display = document.getElementById('cwDecoderText');
        if (!display) return;

        display.innerHTML = '';

        decoderState.currentSegments.forEach(segment => {
            const chars = Array.from(segment.text);
            chars.forEach(char => {
                const span = document.createElement('span');
                span.className = 'cw-decoder-char';
                if (segment.isAbbreviation) {
                    span.classList.add('abbrev');
                }
                span.textContent = char;
                display.appendChild(span);
            });
        });
    }

    function clearDecoderText() {
        const display = document.getElementById('cwDecoderText');
        if (display) {
            display.innerHTML = '';
        }
    }

    // Audio buffer accumulation (called from wfview's audio pipeline)
    function addAudioSamples(samples) {
        if (!decoderState.enabled) return;

        // Add to sample buffer for FFT (keep last FFT_SIZE samples)
        sampleBuffer = sampleBuffer.concat(Array.from(samples));
        if (sampleBuffer.length > FFT_SIZE * 2) {
            sampleBuffer = sampleBuffer.slice(-FFT_SIZE * 2);
        }

        // Add to inference buffer (downsample to 3200 Hz if needed)
        // For now, assume input is roughly at the right rate or we'll resample
        const decimation = Math.floor(samples.length / (SAMPLE_RATE / 100)); // Rough decimation

        for (let i = 0; i < samples.length; i += Math.max(1, decimation)) {
            // Apply gain
            const sample = samples[i] * Math.pow(10, decoderState.gain / 20);
            inferenceBuffer[inferenceWritePos] = sample;
            inferenceWritePos = (inferenceWritePos + 1) % inferenceBuffer.length;
        }
    }

    // Expose public API
    window.CWDecoder = {
        init: initDecoder,
        addAudioSamples: addAudioSamples,
        get state() { return decoderState; }
    };

    // Auto-init when DOM is ready
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', initDecoder);
    } else {
        initDecoder();
    }
})();

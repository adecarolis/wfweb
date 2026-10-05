// CW Decoder for wfweb - Based on web-deep-cw-decoder
// This implementation copies the demo as closely as possible

(function() {
    'use strict';

    // Constants from the demo
    const FFT_SIZE = 4096;  // 2^12
    // Display range centered around BEAT_FREQ_HZ (600 Hz) for symmetric display
    const MIN_FREQ_HZ = 200;   // 400 Hz below beat
    const MAX_FREQ_HZ = 1000;  // 400 Hz above beat
    const DECODABLE_MIN_FREQ_HZ = 400;
    const DECODABLE_MAX_FREQ_HZ = 800;
    const BUFFER_DURATION_S = 12;
    const INFERENCE_INTERVAL_MS = 800;  // Even slower updates for readability
    const SAMPLE_RATE = 4000;
    const BUFFER_SAMPLES = BUFFER_DURATION_S * SAMPLE_RATE;  // 38400

    // Decoder state
    const decoderState = {
        enabled: false,
        loading: false,
        loaded: false,
        gain: 0,
        filterWidth: 250,
        filterFreq: null,
        textBuffer: '',       // accumulated decoded text (rolling)
    };

    const TEXT_BUFFER_MAX = 500; // characters to keep in rolling buffer

    // Fixed center frequency for the filter band (middle of display)
    const CENTER_FREQ_HZ = 800; // Center of 100-1500 Hz range

    // CW beat frequency offset - adjust this to match your radio's sidetone
    // Common values: 600, 700, or 800 Hz
    const BEAT_FREQ_HZ = 600;

    // Auto-tune: listen for a while, find the loudest tone in the displayed
    // band, then move the dial so it lands on BEAT_FREQ_HZ.
    const AUTOTUNE_WINDOW_MS = 1500;  // several elements at any sane CW speed
    const AUTOTUNE_FRAME_MS = 50;     // spectrum snapshots inside that window
    const AUTOTUNE_DEADBAND_HZ = 15;  // finer than the ear - don't nudge for nothing
    const AUTOTUNE_MIN_LEVEL = 10;    // 0-255 analyser units, averaged over the window
    const AUTOTUNE_MIN_RATIO = 1.8;   // peak must stand this far above the band average
    const AUTOTUNE_HOLD_MS = 1600;    // how long the button shows its verdict

    // Audio nodes
    let analyserNode = null;
    let gainNode = null;
    let processorNode = null;
    let audioContext = null;
    let decoderWorker = null;
    let rafId = null;
    let cwWorkletLoaded = false;
    let cwUseWorklet = false;
    let autoTuneTimer = null;
    let autoTuneHoldTimer = null;

    // Audio buffer for inference - sliding window like demo
    const audioBuffer = new Float32Array(BUFFER_SAMPLES);

    // Track new samples accumulated since last inference send
    let totalSamplesAccumulated = 0;
    let lastSentTotal = 0;

    // Canvas
    let canvas = null;
    let ctx2d = null;
    let column = null;
    let renderState = { lastTime: performance.now(), pixelAccumulator: 0 };

    // Color LUT
    let colorLUT = null;

    // AudioWorklet processor: runs decimation on the audio thread to avoid main-thread jitter
    // that causes audio cracking on Windows (see issue #21)
    const CW_PROCESSOR_CODE = [
        'class CWDecoderProcessor extends AudioWorkletProcessor {',
        '  constructor(options) {',
        '    super();',
        '    var targetRate = (options.processorOptions && options.processorOptions.targetRate) || 4000;',
        '    this.df = Math.max(1, Math.round(sampleRate / targetRate));',
        '    this.acc = 0;',
        '    this.cnt = 0;',
        '    this.outBuf = new Float32Array(Math.ceil(128 / this.df) + 1);',
        '  }',
        '  process(inputs, outputs) {',
        '    var input = inputs[0] && inputs[0][0];',
        '    var output = outputs[0] && outputs[0][0];',
        '    if (!input || input.length === 0) { if (output) output.fill(0); return true; }',
        '    if (output) output.set(input);',
        '    var df = this.df, outIdx = 0;',
        '    for (var i = 0; i < input.length; i++) {',
        '      this.acc += input[i];',
        '      this.cnt++;',
        '      if (this.cnt >= df) {',
        '        this.outBuf[outIdx++] = this.acc / df;',
        '        this.acc = 0;',
        '        this.cnt = 0;',
        '      }',
        '    }',
        '    if (outIdx > 0) {',
        '      var arr = this.outBuf.slice(0, outIdx);',
        '      this.port.postMessage(arr, [arr.buffer]);',
        '    }',
        '    return true;',
        '  }',
        '}',
        'registerProcessor("cw-decoder-processor", CWDecoderProcessor);'
    ].join('\n');

    // Initialize
    function init() {
        console.log('[CW Decoder] Initializing...');
        colorLUT = buildColorLUT();
        createUI();
    }

    // HSL to RGB conversion (from demo)
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

    // Build color LUT (from demo)
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

    // Create UI
    function createUI() {
        const cwBar = document.getElementById('cwBar');
        if (!cwBar) {
            setTimeout(createUI, 500);
            return;
        }

        if (document.getElementById('cwDecoderToggle')) return;

        // Add DECODE button to the cw-bar-header (next to CW label)
        const header = cwBar.querySelector('.cw-bar-header');
        if (header) {
            const decodeBtn = document.createElement('button');
            decodeBtn.id = 'cwDecoderToggle';
            decodeBtn.className = 'cw-decoder-btn';
            decodeBtn.textContent = 'DECODE';
            decodeBtn.style.marginLeft = '10px';
            decodeBtn.style.padding = '2px 8px';
            decodeBtn.style.background = '#001a00';
            decodeBtn.style.border = '1px solid #0a0';
            decodeBtn.style.color = '#0a0';
            decodeBtn.style.fontSize = '10px';
            decodeBtn.style.fontWeight = 'bold';
            decodeBtn.style.cursor = 'pointer';
            decodeBtn.style.fontFamily = 'monospace';

            const cwLabel = header.querySelector('.cw-label');
            if (cwLabel) {
                cwLabel.parentNode.insertBefore(decodeBtn, cwLabel.nextSibling);
            } else {
                header.insertBefore(decodeBtn, header.firstChild);
            }
        }

        // Create decoder content section (waterfall + text) - initially hidden
        const section = document.createElement('div');
        section.id = 'cwDecoderSection';
        section.style.display = 'none';  // Hidden when decode is off
        section.innerHTML = `
            <div class="cw-scope-container">
                <canvas id="cwScopeCanvas"></canvas>
                <div id="cwFilterBand"></div>
            </div>
            <div class="cw-decoder-row">
                <div id="cwDecoderText"><span id="cwDecoderTextInner"></span></div>
                <button id="cwAutoTune" class="wf-btn"
                        title="Auto-tune: move the dial so the loudest CW signal sits at ${BEAT_FREQ_HZ} Hz, the centre of the decode window"></button>
            </div>
        `;

        // Insert decoder section BEFORE the macro grids (transmit section)
        const firstChild = cwBar.firstChild;
        cwBar.insertBefore(section, firstChild);

        canvas = document.getElementById('cwScopeCanvas');
        if (canvas) {
            ctx2d = canvas.getContext('2d');
            setupCanvasSizing();
        }

        addStyles();
        setupEventListeners();

        console.log('[CW Decoder] UI created');
    }

    function addStyles() {
        if (document.getElementById('cwDecoderStyles')) return;

        const style = document.createElement('style');
        style.id = 'cwDecoderStyles';
        style.textContent = `
            #cwDecoderSection { margin: 6px 0; border-bottom: 1px solid #0a0; padding-bottom: 6px; }
            #cwDecoderToggle { }
            #cwDecoderToggle:hover { background: #0a0 !important; color: #000 !important; }
            #cwDecoderToggle.active { background: #0a0 !important; color: #000 !important; }
            #cwDecoderToggle.loading { background: #1a1a00 !important; border-color: #aa0 !important; color: #aa0 !important; }
            @media (orientation: portrait) and (max-width: 600px) {
                #cwDecoderToggle { font-size: 10px !important; padding: 6px 5px !important; margin-left: 0 !important; min-height: 28px; letter-spacing: 0; }
                /* The tone spectrogram is a luxury on a phone: the RF
                   waterfall above shows the same signal, and the whole bar
                   must fit under 65% of the scope area. Keep the text line. */
                .cw-scope-container { display: none; }
            }
            .cw-scope-container { position: relative; width: 100%; height: 48px; }
            #cwScopeCanvas { display: block; background: #000; width: 100%; height: 48px; border-radius: 4px; border: 1px solid #0a0; }
            #cwFilterBand { position: absolute; left: 0; right: 0; pointer-events: none; border-top: 1px solid #f00; border-bottom: 1px solid #f00; display: none; }
            #cwFilterBand.active { display: block; }
            .cw-decoder-row { display: flex; align-items: stretch; gap: 6px; margin-top: 6px; }
            #cwAutoTune { flex: 0 0 auto; height: 32px; min-width: 84px; font-size: 11px; letter-spacing: 1px; }
            #cwAutoTune.busy { opacity: 0.6; cursor: progress; }
            #cwAutoTune.ok { background: var(--mode-accent-dim); color: var(--on-accent); border-color: var(--mode-accent); }
            #cwAutoTune.warn { background: #1a1a00; border-color: #aa0; color: #aa0; }
            #cwDecoderText { flex: 1 1 auto; min-width: 0; font-size: 20px; background: #000; border-radius: 4px; border: 1px solid #0a0; height: 32px; color: #0f0; overflow-x: scroll; overflow-y: hidden; scrollbar-width: none; -ms-overflow-style: none; box-sizing: border-box; font-family: 'Courier New', monospace; line-height: 32px; padding: 0 8px; }
            #cwDecoderText::-webkit-scrollbar { display: none; }
            #cwDecoderTextInner { white-space: pre; display: inline; }
            .cw-decoded-call { color: #ff0; cursor: pointer; text-decoration: underline; }
            .cw-decoded-call:hover { color: #000; background: #ff0; }
            /* Phone portrait: with the spectrogram hidden, auto-tune is the
               only way left to centre a signal, so keep the button - just
               narrow enough to leave the decoded text room to read. */
            @media (orientation: portrait) and (max-width: 600px) {
                #cwAutoTune { min-width: 0; height: 28px; padding: 0 8px; font-size: 10px; }
                #cwAutoTune .cw-wide { display: none; }   /* "AUTOTUNE" reads "AUTO" */
                #cwDecoderText { height: 28px; line-height: 28px; font-size: 16px; }
            }
        `;
        document.head.appendChild(style);
    }

    function setupCanvasSizing() {
        if (!canvas) return;
        const rect = canvas.getBoundingClientRect();
        const dpr = window.devicePixelRatio || 1;
        canvas.width = (rect.width || canvas.clientWidth || 300) * dpr;
        canvas.height = (rect.height || canvas.clientHeight || 100) * dpr;
        renderState.pixelAccumulator = 0;
    }

    function setupEventListeners() {
        document.getElementById('cwDecoderToggle')?.addEventListener('click', toggleDecoder);
        document.getElementById('cwAutoTune')?.addEventListener('click', autoTune);
        setAutoTuneState('idle');

        if (canvas) {
            canvas.addEventListener('click', handleCanvasClick);
            canvas.addEventListener('wheel', handleWheel, { passive: false });
        }

        const resizeObserver = new ResizeObserver(() => setupCanvasSizing());
        if (canvas) resizeObserver.observe(canvas);

        // Callsign click: populate QSO field (event delegation on stable container)
        document.getElementById('cwDecoderText')?.addEventListener('click', function(e) {
            const span = e.target.closest('.cw-decoded-call');
            if (!span) return;
            const qsoInput = document.getElementById('cwQsoInput');
            if (qsoInput) {
                qsoInput.value = span.dataset.call;
                if (typeof updateMacroButtons === 'function') updateMacroButtons();
            }
        });
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
        updateButtons();

        try {
            // Wait for wfweb's audio context to be available (poll for up to 5 seconds)
            let waitCount = 0;
            while (!window.audioCtx && waitCount < 50) {
                await new Promise(r => setTimeout(r, 100));
                waitCount++;
            }

            audioContext = window.audioCtx;
            if (!audioContext) {
                console.error('[CW Decoder] No audio context available. Make sure audio is enabled.');
                decoderState.loading = false;
                updateButtons();
                return;
            }

            // Create audio nodes (matching demo)
            analyserNode = audioContext.createAnalyser();
            analyserNode.fftSize = FFT_SIZE;
            analyserNode.smoothingTimeConstant = 0;
            analyserNode.minDecibels = -70;
            analyserNode.maxDecibels = -30;

            gainNode = audioContext.createGain();
            gainNode.gain.value = Math.pow(10, decoderState.gain / 20);

            // Audio capture: prefer AudioWorklet (decimation on audio thread, no
            // main-thread jitter) with ScriptProcessor fallback for non-secure contexts
            if (audioContext.audioWorklet && window.isSecureContext) {
                try {
                    if (!cwWorkletLoaded) {
                        const blob = new Blob([CW_PROCESSOR_CODE], { type: 'application/javascript' });
                        const url = URL.createObjectURL(blob);
                        await audioContext.audioWorklet.addModule(url);
                        URL.revokeObjectURL(url);
                        cwWorkletLoaded = true;
                    }

                    processorNode = new AudioWorkletNode(audioContext, 'cw-decoder-processor', {
                        numberOfInputs: 1,
                        numberOfOutputs: 1,
                        outputChannelCount: [1],
                        processorOptions: { targetRate: SAMPLE_RATE }
                    });

                    processorNode.port.onmessage = (e) => {
                        if (!decoderState.enabled) return;
                        const resampledChunk = e.data;
                        const resampledLen = resampledChunk.length;
                        audioBuffer.copyWithin(0, resampledLen);
                        audioBuffer.set(resampledChunk, BUFFER_SAMPLES - resampledLen);
                        totalSamplesAccumulated += resampledLen;
                    };

                    cwUseWorklet = true;
                    console.log('[CW Decoder] Using AudioWorklet processor');
                } catch (err) {
                    console.warn('[CW Decoder] AudioWorklet failed, falling back to ScriptProcessor:', err);
                    createCWScriptProcessor();
                }
            } else {
                createCWScriptProcessor();
            }

            // Connect nodes
            connectAudioNodes();

            // Start worker — cache-bust: fingerprinter can't see new Worker(literal); append ?v=… by hand.
            decoderWorker = new Worker('cw-decoder-worker.js' + (self.__WFWEB_V__ ? '?v=' + self.__WFWEB_V__ : ''));
            decoderWorker.onmessage = handleWorkerMessage;
            decoderWorker.postMessage({ type: 'loadModel' });

            // Wait for model
            await new Promise((resolve, reject) => {
                const timeout = setTimeout(() => reject(new Error('Timeout')), 30000);
                const check = setInterval(() => {
                    if (decoderState.loaded) {
                        clearInterval(check);
                        clearTimeout(timeout);
                        resolve();
                    }
                }, 100);
            });

            decoderState.enabled = true;
            setupCanvasSizing();
            startRendering();
            updateFilterBand();

        } catch (err) {
            console.error('[CW Decoder] Failed to start:', err);
            stopDecoder();
        } finally {
            decoderState.loading = false;
            updateButtons();
        }
    }

    function createCWScriptProcessor() {
        processorNode = audioContext.createScriptProcessor(2048, 1, 1);
        processorNode.onaudioprocess = (e) => {
            const inputData = e.inputBuffer.getChannelData(0);
            const outputData = e.outputBuffer.getChannelData(0);
            outputData.set(inputData);  // Pass-through

            if (!decoderState.enabled) return;

            const chunkLen = inputData.length;
            const sourceRate = audioContext.sampleRate;
            const decimationFactor = Math.round(sourceRate / SAMPLE_RATE);
            const samplesToProduce = Math.floor(chunkLen / decimationFactor);
            const resampledChunk = new Float32Array(samplesToProduce);

            for (let i = 0; i < samplesToProduce; i++) {
                let sum = 0;
                const startIdx = i * decimationFactor;
                for (let j = 0; j < decimationFactor; j++) {
                    sum += inputData[startIdx + j];
                }
                resampledChunk[i] = sum / decimationFactor;
            }

            const resampledLen = resampledChunk.length;
            audioBuffer.copyWithin(0, resampledLen);
            audioBuffer.set(resampledChunk, BUFFER_SAMPLES - resampledLen);
            totalSamplesAccumulated += resampledLen;
        };
        cwUseWorklet = false;
        console.log('[CW Decoder] Using ScriptProcessor fallback');
    }

    function connectAudioNodes() {
        if (window.audioWorkletNode && window.audioGainNode) {
            window.audioWorkletNode.disconnect();
            window.audioWorkletNode.connect(gainNode);
            gainNode.connect(processorNode);
            processorNode.connect(analyserNode);
            analyserNode.connect(window.audioGainNode);
        } else if (window.audioScriptNode && window.audioGainNode) {
            window.audioScriptNode.disconnect();
            window.audioScriptNode.connect(gainNode);
            gainNode.connect(processorNode);
            processorNode.connect(analyserNode);
            analyserNode.connect(window.audioGainNode);
        }
    }

    function disconnectAudioNodes() {
        if (window.audioGainNode) {
            if (window.audioWorkletNode) {
                processorNode?.disconnect();
                gainNode?.disconnect();
                analyserNode?.disconnect();
                window.audioWorkletNode.disconnect();
                window.audioWorkletNode.connect(window.audioGainNode);
            } else if (window.audioScriptNode) {
                processorNode?.disconnect();
                gainNode?.disconnect();
                analyserNode?.disconnect();
                window.audioScriptNode.disconnect();
                window.audioScriptNode.connect(window.audioGainNode);
            }
        }
    }

    function stopDecoder() {
        if (rafId) {
            cancelAnimationFrame(rafId);
            rafId = null;
        }

        if (processorNode) {
            processorNode.disconnect();
            if (processorNode.port) processorNode.port.onmessage = null;
            processorNode.onaudioprocess = null;
            processorNode = null;
        }

        disconnectAudioNodes();

        if (decoderWorker) {
            decoderWorker.terminate();
            decoderWorker = null;
        }

        cancelAutoTune();

        decoderState.enabled = false;
        decoderState.loaded = false;
        decoderState.loading = false;
        decoderState.textBuffer = '';
        totalSamplesAccumulated = 0;
        lastSentTotal = 0;
        updateText();
        updateButtons();
    }

    function handleWorkerMessage(event) {
        const msg = event.data;
        switch (msg.type) {
            case 'modelLoaded':
                decoderState.loaded = true;
                break;
            case 'inferenceResult':
                if (msg.segments && msg.segments.length > 0) {
                    const newText = msg.segments.map(s => s.text).join('');
                    if (newText.length > 0) {
                        decoderState.textBuffer += newText;
                        if (decoderState.textBuffer.length > TEXT_BUFFER_MAX) {
                            decoderState.textBuffer = decoderState.textBuffer.slice(-TEXT_BUFFER_MAX);
                        }
                        updateText();
                    }
                }
                if (msg.speed_wpm > 0 || msg.frequency_hz > 0) {
                    console.log('[CW Decoder] Detected: ' + msg.frequency_hz.toFixed(0) + ' Hz, ' + msg.speed_wpm.toFixed(1) + ' WPM');
                }
                break;
            case 'error':
                console.error('[CW Decoder Worker] Error:', msg.error);
                break;
        }
    }

    // Spectrogram rendering (exactly like demo)
    function startRendering() {
        if (!canvas || !ctx2d || !analyserNode) return;

        const freqBins = analyserNode.frequencyBinCount;
        const dataArray = new Uint8Array(freqBins);
        renderState.lastTime = performance.now();
        renderState.pixelAccumulator = 0;

        const render = () => {
            if (!decoderState.enabled || !canvas || !analyserNode) return;

            const now = performance.now();
            const dt = (now - renderState.lastTime) / 1000;
            renderState.lastTime = now;

            const pxPerSec = canvas.width / BUFFER_DURATION_S;
            renderState.pixelAccumulator += dt * pxPerSec;

            let step = Math.floor(renderState.pixelAccumulator);
            if (step <= 0) {
                rafId = requestAnimationFrame(render);
                return;
            }
            renderState.pixelAccumulator -= step;
            if (step > canvas.width) step = canvas.width;

            analyserNode.getByteFrequencyData(dataArray);

            // Scroll left (exactly like demo)
            ctx2d.drawImage(
                canvas,
                0, 0, canvas.width, canvas.height,
                -step, 0, canvas.width, canvas.height
            );

            if (!column || column.height !== canvas.height) {
                column = ctx2d.createImageData(1, canvas.height);
            }

            const buf = column.data;
            const nyquist = audioContext.sampleRate / 2;
            const minBin = Math.floor((MIN_FREQ_HZ / nyquist) * (freqBins - 1));
            const maxBin = Math.min(
                freqBins - 1,
                Math.floor((Math.min(MAX_FREQ_HZ, nyquist) / nyquist) * (freqBins - 1))
            );

            for (let y = 0; y < canvas.height; y++) {
                const invY = canvas.height - 1 - y;
                const binRange = maxBin - minBin;
                const idx = minBin + Math.floor((invY / Math.max(1, canvas.height - 1)) * binRange);
                const v = dataArray[idx];
                const [r, g, b] = colorLUT[v];
                const p = y * 4;
                buf[p] = r;
                buf[p + 1] = g;
                buf[p + 2] = b;
                buf[p + 3] = 255;
            }

            for (let i = 0; i < step; i++) {
                ctx2d.putImageData(column, canvas.width - step + i, 0);
            }

            // Run inference periodically
            if (now - (renderState.lastInferenceTime || 0) > INFERENCE_INTERVAL_MS) {
                runInference();
                renderState.lastInferenceTime = now;
            }

            rafId = requestAnimationFrame(render);
        };

        renderState.lastInferenceTime = performance.now();
        rafId = requestAnimationFrame(render);
    }

    function runInference() {
        if (!decoderState.enabled || !decoderWorker || !decoderState.loaded) return;

        // Send only samples accumulated since last call (ggmorse maintains internal state)
        const newCount = Math.min(totalSamplesAccumulated - lastSentTotal, BUFFER_SAMPLES);
        if (newCount <= 0) return;

        const start = BUFFER_SAMPLES - newCount;
        const newSamples = audioBuffer.slice(start);
        lastSentTotal = totalSamplesAccumulated;

        decoderWorker.postMessage({
            type: 'runInference',
            audioBuffer: newSamples
        }, [newSamples.buffer]);
    }

    // Ham radio callsign detector
    // Covers the vast majority of ITU callsign formats:
    // up to 3 prefix chars (letters/digits), one district digit, 1-4 suffix letters.
    // The only three-character calls accepted are US 1x1 special events (K/N/W,
    // digit, letter — N6O, K1W): any wider and short garbage decodes start
    // matching.
    function isCallsign(word) {
        if (word.length === 3) return /^[KNW][0-9][A-Z]$/.test(word);
        if (word.length < 4 || word.length > 8) return false;
        return /^[A-Z0-9]{1,3}[0-9][A-Z]{1,4}$/.test(word);
    }

    function escapeHtml(str) {
        return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    }

    // Text display - rolling teleprinter style, newest text on the right
    // Words matching callsign format (and differing from CALL field) become clickable links
    function updateText() {
        const container = document.getElementById('cwDecoderText');
        const inner = document.getElementById('cwDecoderTextInner');
        if (!container || !inner) return;

        const myCall = (document.getElementById('cwCallInput')?.value || '').trim().toUpperCase();
        const text = decoderState.textBuffer;

        // Split on runs of alphanumeric chars (preserving separators between them)
        const parts = text.split(/([A-Z0-9]+)/);

        let html = '';
        for (const part of parts) {
            if (/^[A-Z0-9]+$/.test(part) && isCallsign(part) && part !== myCall) {
                html += `<span class="cw-decoded-call" data-call="${part}">${part}</span>`;
            } else {
                html += escapeHtml(part);
            }
        }

        inner.innerHTML = html;
        container.scrollLeft = container.scrollWidth;
    }

    function updateButtons() {
        const toggleBtn = document.getElementById('cwDecoderToggle');
        const section = document.getElementById('cwDecoderSection');

        if (toggleBtn) {
            if (decoderState.loading) {
                toggleBtn.textContent = 'LOADING...';
                toggleBtn.className = 'loading';
            } else if (decoderState.enabled) {
                toggleBtn.textContent = 'DECODE';
                toggleBtn.className = 'active';
            } else {
                toggleBtn.textContent = 'DECODE';
                toggleBtn.className = '';
            }
        }

        // Show/hide decoder section based on state
        if (section) {
            section.style.display = decoderState.enabled ? 'block' : 'none';
        }
    }

    // Filter band display - fixed at beat frequency where CW signals appear
    function updateFilterBand() {
        const band = document.getElementById('cwFilterBand');
        if (!band) return;

        // Filter band is centered on BEAT_FREQ_HZ (where radio puts CW tones)
        const range = MAX_FREQ_HZ - MIN_FREQ_HZ;
        const half = decoderState.filterWidth / 2;
        const lower = Math.max(MIN_FREQ_HZ, BEAT_FREQ_HZ - half);
        const upper = Math.min(MAX_FREQ_HZ, BEAT_FREQ_HZ + half);

        const topPercent = ((MAX_FREQ_HZ - upper) / range) * 100;
        const heightPercent = ((upper - lower) / range) * 100;

        band.style.top = `${topPercent}%`;
        band.style.height = `${heightPercent}%`;
        band.classList.add('active');
    }

    // ------------------------------------------------------------------
    // Tuning
    // ------------------------------------------------------------------

    // Read a value the host page owns (currentFreq, currentMode, send, ...).
    // The decoder also runs framed, so fall back to the parent document.
    function host(prop) {
        if (window[prop] !== undefined) return window[prop];
        try {
            if (window.parent && window.parent !== window && window.parent[prop] !== undefined) {
                return window.parent[prop];
            }
        } catch (e) { /* cross-origin parent */ }
        return undefined;
    }

    function hostSend(msg) {
        const send = host('send');
        if (typeof send === 'function') send(msg);
    }

    // How far to move the dial to drop an audio tone onto the sidetone pitch.
    // Icom receives normal CW on the LSB side and CW-R on the USB side
    // (IC-7600 manual, "About CW reverse mode"), so the dial moves the
    // opposite way in reverse: a tone above the pitch means the signal sits
    // below the dial in CW, above it in CW-R.
    function dialDeltaForTone(toneHz) {
        const reverse = String(host('currentMode') || '').toUpperCase() === 'CW-R';
        return reverse ? (toneHz - BEAT_FREQ_HZ) : (BEAT_FREQ_HZ - toneHz);
    }

    // Returns the dial move actually sent, or null if the host has no VFO yet.
    function tuneToTone(toneHz) {
        const dial = host('currentFreq') || 0;
        if (dial <= 0) return null;

        const delta = dialDeltaForTone(toneHz);
        hostSend({ cmd: 'setFrequency', value: Math.round(dial + delta) });
        return delta;
    }

    // Auto-tune: average the spectrum over a short window, take the loudest
    // tone in the displayed band and pull it to the centre of the decode
    // window. CW is on/off keyed, so averaging weights the tone by its duty
    // cycle while noise averages flat - a single burst can't win the peak.
    // Searching only MIN_FREQ_HZ..MAX_FREQ_HZ also caps the pull at +/-400 Hz,
    // near the +/-500 Hz capture range of Icom's own AUTO TUNE.
    function autoTune() {
        if (autoTuneTimer) return;                       // already listening
        if (!decoderState.enabled || !analyserNode || !audioContext) return;

        const freqBins = analyserNode.frequencyBinCount;
        const binHz = (audioContext.sampleRate / 2) / freqBins;
        const minBin = Math.max(1, Math.round(MIN_FREQ_HZ / binHz));
        const maxBin = Math.min(freqBins - 2, Math.round(MAX_FREQ_HZ / binHz));
        if (maxBin - minBin < 4) return;

        const spectrum = new Uint8Array(freqBins);
        const sums = new Float64Array(maxBin - minBin + 1);
        let frames = 0;

        setAutoTuneState('busy');

        autoTuneTimer = setInterval(() => {
            if (!decoderState.enabled || !analyserNode) { cancelAutoTune(); return; }

            analyserNode.getByteFrequencyData(spectrum);
            for (let b = minBin; b <= maxBin; b++) sums[b - minBin] += spectrum[b];
            frames++;

            if (frames * AUTOTUNE_FRAME_MS < AUTOTUNE_WINDOW_MS) return;

            clearInterval(autoTuneTimer);
            autoTuneTimer = null;
            finishAutoTune(sums, minBin, binHz, frames);
        }, AUTOTUNE_FRAME_MS);
    }

    function cancelAutoTune() {
        if (autoTuneTimer) { clearInterval(autoTuneTimer); autoTuneTimer = null; }
        if (autoTuneHoldTimer) { clearTimeout(autoTuneHoldTimer); autoTuneHoldTimer = null; }
        setAutoTuneState('idle');
    }

    function finishAutoTune(sums, minBin, binHz, frames) {
        let peak = 1;
        let total = 0;
        for (let i = 0; i < sums.length; i++) {
            total += sums[i];
            if (i > 0 && i < sums.length - 1 && sums[i] > sums[peak]) peak = i;
        }

        const level = sums[peak] / frames;               // 0-255, as the analyser scales it
        const average = total / sums.length / frames;
        if (level < AUTOTUNE_MIN_LEVEL || level < average * AUTOTUNE_MIN_RATIO) {
            console.log('[CW Decoder] Auto-tune: no signal (peak ' + level.toFixed(0) +
                        ', band average ' + average.toFixed(0) + ')');
            setAutoTuneState('warn', 'NO SIG');
            return;
        }

        // Parabolic interpolation across the peak and its neighbours: one FFT
        // bin is ~12 Hz wide, coarser than the deadband we want to hold.
        const a = sums[peak - 1], b = sums[peak], c = sums[peak + 1];
        const denom = a - 2 * b + c;
        const frac = denom !== 0 ? Math.max(-0.5, Math.min(0.5, 0.5 * (a - c) / denom)) : 0;
        const toneHz = (minBin + peak + frac) * binHz;

        if (Math.abs(dialDeltaForTone(toneHz)) < AUTOTUNE_DEADBAND_HZ) {
            console.log('[CW Decoder] Auto-tune: already centred (' + toneHz.toFixed(0) + ' Hz)');
            setAutoTuneState('ok', '\u2713 ' + Math.round(toneHz) + ' Hz');
            return;
        }

        const delta = tuneToTone(toneHz);
        if (delta === null) {
            setAutoTuneState('warn', 'NO VFO');
            return;
        }
        console.log('[CW Decoder] Auto-tune: ' + toneHz.toFixed(0) + ' Hz tone, dial ' +
                    (delta > 0 ? '+' : '') + Math.round(delta) + ' Hz');
        setAutoTuneState('ok', (delta > 0 ? '+' : '\u2212') + Math.abs(Math.round(delta)) + ' Hz');
    }

    // Button label doubles as the auto-tune readout: what it heard, or why not.
    function setAutoTuneState(state, label) {
        const btn = document.getElementById('cwAutoTune');
        if (!btn) return;

        if (autoTuneHoldTimer) { clearTimeout(autoTuneHoldTimer); autoTuneHoldTimer = null; }
        btn.className = 'wf-btn' + (state === 'idle' ? '' : ' ' + state);

        if (state === 'idle') {
            btn.innerHTML = '\u2316 AUTO<span class="cw-wide">TUNE</span>';
        } else if (state === 'busy') {
            btn.textContent = 'LISTENING';
        } else {
            btn.textContent = label;
            autoTuneHoldTimer = setTimeout(() => {
                autoTuneHoldTimer = null;
                setAutoTuneState('idle');
            }, AUTOTUNE_HOLD_MS);
        }
    }

    // Canvas interactions
    function handleCanvasClick(event) {
        // Click a tone to tune it to the centre of the decode window.
        const rect = canvas.getBoundingClientRect();
        const invY = rect.height - (event.clientY - rect.top);
        const clickedAudioFreq = MIN_FREQ_HZ + (invY / rect.height) * (MAX_FREQ_HZ - MIN_FREQ_HZ);
        tuneToTone(clickedAudioFreq);
    }

    function handleWheel(e) {
        e.preventDefault();

        const currentFreq = host('currentFreq') || 0;
        if (currentFreq <= 0) return;

        // Tune radio up/down by 50 Hz
        const step = 50;
        let newFreq = currentFreq;

        if (e.deltaY < 0) {
            newFreq += step;
        } else if (e.deltaY > 0) {
            newFreq -= step;
        }

        hostSend({ cmd: 'setFrequency', value: Math.round(newFreq) });
    }

    // Expose API
    window.CWDecoder = {
        init: init,
        get state() { return decoderState; }
    };

    // Auto-init
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})();

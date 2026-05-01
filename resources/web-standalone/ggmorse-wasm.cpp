#include <emscripten.h>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include "ggmorse/ggmorse.h"

static GGMorse* g_ggMorse = nullptr;
static std::vector<float> g_buffer;
static std::string g_newText;
static float g_sampleRate = 3200.0f;

static void createInstance(float sampleRate) {
    delete g_ggMorse;

    GGMorse::Parameters params;
    params.sampleRateInp = sampleRate;
    params.sampleRateOut = sampleRate;
    params.samplesPerFrame = GGMorse::kDefaultSamplesPerFrame;
    params.sampleFormatInp = GGMORSE_SAMPLE_FORMAT_F32;
    params.sampleFormatOut = GGMORSE_SAMPLE_FORMAT_F32;

    g_ggMorse = new GGMorse(params);

    auto paramsDecode = GGMorse::getDefaultParametersDecode();
    paramsDecode.frequency_hz = -1.0f;          // auto-detect pitch
    paramsDecode.speed_wpm = -1.0f;             // auto-detect speed
    paramsDecode.frequencyRangeMin_hz = 200.0f;
    paramsDecode.frequencyRangeMax_hz = 1200.0f;
    g_ggMorse->setParametersDecode(paramsDecode);
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
void ggmorse_init(float sampleRate) {
    g_sampleRate = sampleRate;
    g_buffer.clear();
    g_newText.clear();
    createInstance(sampleRate);
}

EMSCRIPTEN_KEEPALIVE
void ggmorse_queue(float* samples, int n) {
    if (!g_ggMorse || !samples || n <= 0) return;
    g_buffer.insert(g_buffer.end(), samples, samples + n);
}

EMSCRIPTEN_KEEPALIVE
int ggmorse_decode() {
    if (!g_ggMorse || g_buffer.empty()) return 0;

    const uint32_t sampleSizeBytes = sizeof(float);
    size_t readOffset = 0;
    const size_t totalSamples = g_buffer.size();

    GGMorse::CBWaveformInp cb = [&](void* data, uint32_t nMaxBytes) -> uint32_t {
        const uint32_t nMaxSamples = nMaxBytes / sampleSizeBytes;
        const size_t remaining = totalSamples - readOffset;

        // Must provide exactly a full frame or 0 — never a partial frame.
        // Remaining samples below one frame are kept for the next decode() call.
        if (remaining < nMaxSamples) return 0;

        memcpy(data, g_buffer.data() + readOffset, nMaxBytes);
        readOffset += nMaxSamples;
        return nMaxBytes;
    };

    g_ggMorse->decode(cb);

    // Keep any unconsumed tail samples for the next decode() call
    if (readOffset > 0 && readOffset < totalSamples) {
        g_buffer.erase(g_buffer.begin(), g_buffer.begin() + readOffset);
    } else {
        g_buffer.clear();
    }

    // Collect newly decoded text
    GGMorse::TxRx rxData;
    int nNew = g_ggMorse->takeRxData(rxData);
    if (nNew > 0) {
        for (auto byte : rxData) {
            if (byte >= 32 && byte < 128) {
                g_newText += (char)byte;
            }
        }
    }

    return nNew;
}

EMSCRIPTEN_KEEPALIVE
const char* ggmorse_get_text() {
    static std::string result;
    result = g_newText;
    g_newText.clear();
    return result.c_str();
}

EMSCRIPTEN_KEEPALIVE
float ggmorse_get_frequency() {
    if (!g_ggMorse) return 0.0f;
    return g_ggMorse->getStatistics().estimatedPitch_Hz;
}

EMSCRIPTEN_KEEPALIVE
float ggmorse_get_speed() {
    if (!g_ggMorse) return 0.0f;
    return g_ggMorse->getStatistics().estimatedSpeed_wpm;
}

EMSCRIPTEN_KEEPALIVE
void ggmorse_reset() {
    g_buffer.clear();
    g_newText.clear();
    createInstance(g_sampleRate);
}

} // extern "C"

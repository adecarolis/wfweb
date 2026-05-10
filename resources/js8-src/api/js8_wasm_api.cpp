// Implementation of the C entry points declared in js8_wasm_api.h.
//
// Wraps:
//   - JS8::encode (free function, namespace JS8) for TX
//   - JS8::DecoderImpl (the class we hoisted out of upstream's Worker)
//     for RX, fed via the global `dec_data` declared in commons.h
//
// The decode side mimics what JS8Call's MainWindow does on slot boundary:
// fill dec_data.d2 + params, then invoke the decoder, then collect the
// emitted Decoded events into an internal queue.

#include "js8_wasm_api.h"

#include "JS8_Mode/JS8.h"
#include "JS8_Include/commons.h"
#include "QLoggingCategory"

// `decoder_js8` is declared (Q_DECLARE_LOGGING_CATEGORY) in three of the
// codec headers but its definition lives upstream in Decoder.cpp, which
// we don't vendor. Provide the definition here so the link resolves.
Q_LOGGING_CATEGORY(decoder_js8, "decoder.js8", QtWarningMsg)

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// JS8.cpp expects these globals (declared `extern` in commons.h).
struct dec_data dec_data;
struct specData specData;
std::mutex fftw_mutex;

// JS8.cpp's DecoderImpl is a class inside namespace JS8 that we need to
// reach from this TU. Forward-declare it; full definition lives in
// JS8.cpp and is reachable at link time.
namespace JS8 {
class DecoderImpl;
}

// We can't construct DecoderImpl across TUs without its definition. The
// API instead opens up a thin factory inside JS8.cpp via two free
// functions exposed at the bottom of that file: js8_make_decoder() and
// js8_run_decoder(). For Day 4 we declare them here; Day 4 also adds
// matching definitions to JS8.cpp in a follow-up edit.
namespace JS8 {
extern std::unique_ptr<DecoderImpl> js8_make_decoder(struct dec_data& data);
extern void js8_run_decoder(DecoderImpl& impl, ::JS8::Event::Emitter emit);
} // namespace JS8

/* ============== js8_decoder ============================================ */

struct js8_decoder {
    int submode_bit;
    std::unique_ptr<JS8::DecoderImpl> impl;
    std::vector<float> ring;
    std::deque<std::string> outbox;
};

/* ============== Encoder ================================================ */

extern "C" int js8_encode(int frame_type, const char* msg, int* tones_out) {
    if (!msg || !tones_out) return -1;
    // Length precondition — must be exactly 12 chars from the JS8 alphabet.
    int n = 0;
    while (msg[n] && n <= 12) ++n;
    if (n != 12) return -1;
    try {
        // ORIGINAL Costas for Normal mode; MODIFIED for the others. JS8.h
        // stores both; pick by speed-mode. For day 4 we always use the
        // Normal Costas since the encoder's submode is the caller's choice
        // (encoded into frame_type's upper bits in real JS8 use).
        auto const& costas = JS8::Costas::array(JS8::Costas::Type::ORIGINAL);
        JS8::encode(frame_type, costas, msg, tones_out);
        return 0;
    } catch (...) {
        return -1;
    }
}

/* ============== Decoder lifecycle ===================================== */

extern "C" js8_decoder* js8_decoder_new(int submode) {
    auto* dec = new (std::nothrow) js8_decoder;
    if (!dec) return nullptr;
    dec->submode_bit = submode;
    dec->impl = JS8::js8_make_decoder(::dec_data);
    return dec;
}

extern "C" void js8_decoder_free(js8_decoder* dec) {
    delete dec;
}

extern "C" int js8_decoder_push(js8_decoder* dec, const float* samples, int n) {
    if (!dec || !samples || n <= 0) return 0;
    // Convert 12 kHz float [-1,1] → int16 and stage in dec_data.d2.
    // dec_data.d2 is fixed-size (JS8_RX_SAMPLE_SIZE = 60s * 12000 = 720k
    // samples). For Day 4 we just always keep the most recent samples
    // up to the buffer cap — Phase 1 will refine this with a proper ring.
    constexpr int cap = JS8_RX_SAMPLE_SIZE;
    if (dec->ring.size() + static_cast<size_t>(n) > static_cast<size_t>(cap)) {
        size_t drop = (dec->ring.size() + n) - cap;
        dec->ring.erase(dec->ring.begin(),
                         dec->ring.begin() + static_cast<long>(drop));
    }
    dec->ring.insert(dec->ring.end(), samples, samples + n);
    return n;
}

extern "C" int js8_decoder_run(js8_decoder* dec) {
    if (!dec || !dec->impl) return 0;

    // Stage the buffered audio into dec_data.
    int n = static_cast<int>(std::min(dec->ring.size(),
                                      static_cast<size_t>(JS8_RX_SAMPLE_SIZE)));
    for (int i = 0; i < n; ++i) {
        float s = std::max(-1.0f, std::min(1.0f, dec->ring[i]));
        ::dec_data.d2[i] = static_cast<int16_t>(s * 32767.0f);
    }
    ::dec_data.params.kin = n;
    ::dec_data.params.newdat = true;
    ::dec_data.params.nsubmodes = dec->submode_bit;

    int emitted = 0;
    JS8::js8_run_decoder(*dec->impl, [&](::JS8::Event::Variant const& ev) {
        if (auto p = std::get_if<::JS8::Event::Decoded>(&ev)) {
            char buf[512];
            int written = std::snprintf(buf, sizeof(buf),
                "{\"snr\":%d,\"dt\":%g,\"freq\":%g,\"text\":\"%s\","
                "\"type\":%d,\"quality\":%g,\"mode\":%d,\"utc\":%d}",
                p->snr, static_cast<double>(p->xdt),
                static_cast<double>(p->frequency),
                p->data.c_str(), p->type,
                static_cast<double>(p->quality),
                p->mode, p->utc);
            if (written > 0) {
                dec->outbox.emplace_back(buf, std::min(written, (int)sizeof(buf)));
                ++emitted;
            }
        }
    });
    return emitted;
}

extern "C" char* js8_decoder_pop(js8_decoder* dec) {
    if (!dec || dec->outbox.empty()) return nullptr;
    std::string s = std::move(dec->outbox.front());
    dec->outbox.pop_front();
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, s.data(), s.size());
    out[s.size()] = '\0';
    return out;
}

extern "C" void js8_free_string(char* s) {
    std::free(s);
}

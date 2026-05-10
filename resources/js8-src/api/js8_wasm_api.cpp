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

// We can't construct DecoderImpl across TUs without its definition
// (it lives inside JS8.cpp's anonymous namespace). The API instead
// opens up a thin factory inside JS8.cpp via free functions returning
// raw pointers; we manage lifetime here.
namespace JS8 {
extern DecoderImpl* js8_make_decoder(struct dec_data& data);
extern void         js8_delete_decoder(DecoderImpl* p);
extern void         js8_run_decoder(DecoderImpl& impl, ::JS8::Event::Emitter emit);
} // namespace JS8

/* ============== js8_decoder ============================================ */

struct js8_decoder {
    int submode_bit;
    JS8::DecoderImpl* impl;
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
    // The caller passes a Varicode::SubmodeType *ID* (Normal=0, Fast=1,
    // Turbo=2, Slow=4, Ultra=8). The decoder selects modes via a
    // `nsubmodes` *bitmask* where ModeA=1, ModeB=2, ModeC=4, ModeE=8,
    // ModeI=16 (== 1<<{0,1,2,3,4}). Translate so the JS side doesn't
    // have to think about it.
    int bit = 0;
    switch (submode) {
        case 0: bit = 1;  break; // JS8CallNormal → ModeA
        case 1: bit = 2;  break; // JS8CallFast   → ModeB
        case 2: bit = 4;  break; // JS8CallTurbo  → ModeC
        case 4: bit = 8;  break; // JS8CallSlow   → ModeE
        case 8: bit = 16; break; // JS8CallUltra  → ModeI
        default: bit = 1; break; // default to Normal
    }
    dec->submode_bit = bit;
    dec->impl = JS8::js8_make_decoder(::dec_data);
    return dec;
}

extern "C" void js8_decoder_free(js8_decoder* dec) {
    if (!dec) return;
    if (dec->impl) JS8::js8_delete_decoder(dec->impl);
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
    // Decode-window defaults — JS8Call's mainwindow.cpp sets these per
    // slot. For the WASM bridge we cover the whole pushed buffer with a
    // wide-band filter (200..2800 Hz) and let the decoder hunt for sync
    // anywhere in it. Phase 1 may refine these (e.g., narrow the band
    // around a user-selected nfqso to reduce false-decode rate).
    ::dec_data.params.kposA = 0;
    ::dec_data.params.kszA  = n;
    ::dec_data.params.kposB = 0;
    ::dec_data.params.kszB  = n;
    ::dec_data.params.kposC = 0;
    ::dec_data.params.kszC  = n;
    ::dec_data.params.kposE = 0;
    ::dec_data.params.kszE  = n;
    ::dec_data.params.kposI = 0;
    ::dec_data.params.kszI  = n;
    ::dec_data.params.nfa = 200;
    ::dec_data.params.nfb = 2800;
    ::dec_data.params.nfqso = 1500;
    ::dec_data.params.syncStats = false;
    ::dec_data.params.nutc = 0;

    int emitted = 0;
    JS8::js8_run_decoder(*dec->impl, [&](::JS8::Event::Variant const& ev) {
        char buf[512];
        int written = 0;
        if (auto p = std::get_if<::JS8::Event::Decoded>(&ev)) {
            written = std::snprintf(buf, sizeof(buf),
                "{\"event\":\"decoded\","
                "\"snr\":%d,\"dt\":%g,\"freq\":%g,\"text\":\"%s\","
                "\"type\":%d,\"quality\":%g,\"mode\":%d,\"utc\":%d}",
                p->snr, static_cast<double>(p->xdt),
                static_cast<double>(p->frequency),
                p->data.c_str(), p->type,
                static_cast<double>(p->quality),
                p->mode, p->utc);
            ++emitted;
        } else if (auto p = std::get_if<::JS8::Event::DecodeStarted>(&ev)) {
            written = std::snprintf(buf, sizeof(buf),
                "{\"event\":\"started\",\"submodes\":%d}", p->submodes);
        } else if (auto p = std::get_if<::JS8::Event::DecodeFinished>(&ev)) {
            written = std::snprintf(buf, sizeof(buf),
                "{\"event\":\"finished\",\"decoded\":%zu}", p->decoded);
        } else if (auto p = std::get_if<::JS8::Event::SyncStart>(&ev)) {
            written = std::snprintf(buf, sizeof(buf),
                "{\"event\":\"sync_start\",\"position\":%d,\"size\":%d}",
                p->position, p->size);
        } else if (auto p = std::get_if<::JS8::Event::SyncState>(&ev)) {
            written = std::snprintf(buf, sizeof(buf),
                "{\"event\":\"sync\",\"mode\":%d,\"freq\":%g,\"dt\":%g,"
                "\"kind\":\"%s\"}",
                p->mode, static_cast<double>(p->frequency),
                static_cast<double>(p->dt),
                p->type == ::JS8::Event::SyncState::Type::CANDIDATE ?
                    "candidate" : "decoded");
        }
        if (written > 0) {
            dec->outbox.emplace_back(buf, std::min(written, (int)sizeof(buf)));
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

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
#include "JS8_Mode/DecodedText.h"
#include "JS8_Main/Varicode.h"
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

extern "C" int js8_encode(int submode, int frame_type,
                          const char* msg, int* tones_out) {
    if (!msg || !tones_out) return -1;
    // Length precondition — must be exactly 12 chars from the JS8 alphabet.
    int n = 0;
    while (msg[n] && n <= 12) ++n;
    if (n != 12) return -1;
    try {
        // Normal uses the original FT8 Costas array; every other submode
        // uses the modified per-position Costas to disambiguate which
        // speed mode a sync candidate belongs to.
        auto costasType = (submode == 0)
            ? JS8::Costas::Type::ORIGINAL
            : JS8::Costas::Type::MODIFIED;
        auto const& costas = JS8::Costas::array(costasType);
        JS8::encode(frame_type, costas, msg, tones_out);
        return 0;
    } catch (...) {
        return -1;
    }
}

/* ============== Decoder lifecycle ===================================== */

extern "C" js8_decoder* js8_decoder_new(int /*submode*/) {
    auto* dec = new (std::nothrow) js8_decoder;
    if (!dec) return nullptr;
    // Enable all five submodes — Normal=1, Fast=2, Turbo("JS8 40")=4,
    // Slow=8, Ultra("JS8 60")=16 → bitmask 31. JS8Call 3.0.0 exposes
    // JS8 60 (formerly the upstream-only "Ultra" mode) in the official
    // client, so wfweb can decode it interoperably. The `submode`
    // argument is kept in the signature for callers that still pass it
    // but is otherwise ignored.
    dec->submode_bit = 1 | 2 | 4 | 8 | 16;
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

// Pull `dec->ring` into dec_data.d2 (with int16 quantization), set the
// common per-call parameters, and return the staged sample count. The
// caller still needs to fill in nsubmodes + kposX/kszX per mode.
static int js8_stage_dec_data(js8_decoder* dec) {
    int n = static_cast<int>(std::min(dec->ring.size(),
                                      static_cast<size_t>(JS8_RX_SAMPLE_SIZE)));
    for (int i = 0; i < n; ++i) {
        float s = std::max(-1.0f, std::min(1.0f, dec->ring[i]));
        ::dec_data.d2[i] = static_cast<int16_t>(s * 32767.0f);
    }
    ::dec_data.params.kin     = n;
    ::dec_data.params.newdat  = true;
    ::dec_data.params.nfa     = 200;
    ::dec_data.params.nfb     = 2800;
    ::dec_data.params.nfqso   = 1500;
    ::dec_data.params.syncStats = false;
    ::dec_data.params.nutc    = 0;
    ::dec_data.params.kposI   = 0;   // Ultra is intentionally never decoded
    ::dec_data.params.kszI    = 0;
    return n;
}

// Run the decoder and serialize whatever events come out into dec->outbox.
// Shared between js8_decoder_run and js8_decoder_run_modes — the only
// thing that differs between those is how nsubmodes / kposX / kszX are
// populated above this call.
static int js8_drain_decoder(js8_decoder* dec) {
    auto escapeJson = [](std::string const &in) {
        std::string out;
        out.reserve(in.size() + 8);
        for (char c : in) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if ((unsigned char)c < 0x20) out += '?';
                    else                          out += c;
            }
        }
        return out;
    };

    int emitted = 0;
    JS8::js8_run_decoder(*dec->impl, [&](::JS8::Event::Variant const& ev) {
        char buf[512];
        int written = 0;
        if (auto p = std::get_if<::JS8::Event::Decoded>(&ev)) {
            DecodedText dt(*p);
            std::string textHuman = dt.message().toStdString();
            std::string textRaw   = p->data;
            std::string textHumanEsc = escapeJson(textHuman);
            std::string textRawEsc   = escapeJson(textRaw);
            written = std::snprintf(buf, sizeof(buf),
                "{\"event\":\"decoded\","
                "\"snr\":%d,\"dt\":%g,\"freq\":%g,"
                "\"text\":\"%s\",\"raw\":\"%s\",\"frameType\":%u,"
                "\"type\":%d,\"quality\":%g,\"mode\":%d,\"utc\":%d}",
                p->snr, static_cast<double>(p->xdt),
                static_cast<double>(p->frequency),
                textHumanEsc.c_str(), textRawEsc.c_str(),
                static_cast<unsigned>(dt.frameType()),
                p->type, static_cast<double>(p->quality),
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

extern "C" int js8_decoder_run(js8_decoder* dec) {
    if (!dec || !dec->impl) return 0;
    int n = js8_stage_dec_data(dec);
    // Legacy path used by the corpus tests: nsubmodes from the decoder's
    // saved bit, each mode's window pinned to the first kszX samples,
    // clamped to one slot. New callers should prefer run_modes.
    ::dec_data.params.nsubmodes = dec->submode_bit;
    auto clamp = [&](int max) { return std::min(n, max); };
    ::dec_data.params.kposA = 0;
    ::dec_data.params.kszA  = clamp(JS8A_TX_SECONDS * JS8_RX_SAMPLE_RATE);
    ::dec_data.params.kposB = 0;
    ::dec_data.params.kszB  = clamp(JS8B_TX_SECONDS * JS8_RX_SAMPLE_RATE);
    ::dec_data.params.kposC = 0;
    ::dec_data.params.kszC  = clamp(JS8C_TX_SECONDS * JS8_RX_SAMPLE_RATE);
    ::dec_data.params.kposE = 0;
    ::dec_data.params.kszE  = clamp(JS8E_TX_SECONDS * JS8_RX_SAMPLE_RATE);
    ::dec_data.params.kposI = 0;
    ::dec_data.params.kszI  = clamp(JS8I_TX_SECONDS * JS8_RX_SAMPLE_RATE);
    return js8_drain_decoder(dec);
}

extern "C" int js8_decoder_run_modes(js8_decoder* dec, int nsubmodes,
                                     int kposA, int kszA,
                                     int kposB, int kszB,
                                     int kposC, int kszC,
                                     int kposE, int kszE,
                                     int kposI, int kszI) {
    if (!dec || !dec->impl) return 0;
    js8_stage_dec_data(dec);
    ::dec_data.params.nsubmodes = nsubmodes;
    ::dec_data.params.kposA = kposA; ::dec_data.params.kszA = kszA;
    ::dec_data.params.kposB = kposB; ::dec_data.params.kszB = kszB;
    ::dec_data.params.kposC = kposC; ::dec_data.params.kszC = kszC;
    ::dec_data.params.kposE = kposE; ::dec_data.params.kszE = kszE;
    ::dec_data.params.kposI = kposI; ::dec_data.params.kszI = kszI;
    return js8_drain_decoder(dec);
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

/* ============== Packer (free-text + compound frames) ================== */

extern "C" char* js8_pack(const char* mycall, const char* mygrid,
                          const char* selectedCall, const char* text,
                          int submode) {
    if (!text) return nullptr;
    try {
        QString qMycall = mycall ? QString(mycall) : QString();
        QString qMygrid = mygrid ? QString(mygrid) : QString();
        QString qSel    = selectedCall ? QString(selectedCall) : QString();
        QString qText   = QString(text);

        // forceIdentify=true matches the official JS8Call client: when the
        // line is a bare data frame with no DX target and mycall isn't
        // already in it, buildMessageFrames prepends "MYCALL: " so the
        // receiver sees who said it. Without this flag, "HELLO" goes out
        // as one literal frame; with it, "K1FM: HELLO" goes out as the
        // expected two-frame compound. Safe to leave on for directed
        // messages too — the prepend only triggers for unaddressed data.
        auto frames = Varicode::buildMessageFrames(
            qMycall, qMygrid, qSel, qText,
            /*forceIdentify*/ true, /*forceData*/ false, submode, nullptr);

        // Build JSON manually — Qt's JSON classes aren't shimmed.
        std::string out = "[";
        bool first = true;
        for (auto const& pair : frames) {
            if (!first) out += ",";
            first = false;
            // Frames are 12-char raw alphabet — no JSON-escape needed.
            out += "{\"frame\":\"";
            out += pair.first.toStdString();
            out += "\",\"type\":";
            out += std::to_string(pair.second);
            out += "}";
        }
        out += "]";

        char* dup = static_cast<char*>(std::malloc(out.size() + 1));
        if (!dup) return nullptr;
        std::memcpy(dup, out.data(), out.size());
        dup[out.size()] = '\0';
        return dup;
    } catch (...) {
        return nullptr;
    }
}

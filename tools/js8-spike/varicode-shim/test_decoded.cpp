// Smoke-test: construct a DecodedText from a synthetic Decoded event,
// pull a few fields out, print to stdout. Verifies that DecodedText.cpp
// links and runs against the Qt-shim + Varicode-stub.

#include "DecodedText.h"
#include <cstdio>

int main() {
    JS8::Event::Decoded ev{};
    ev.utc       = 123456;
    ev.snr       = -12;
    ev.xdt       = 0.5f;
    ev.frequency = 1500.0f;
    ev.data      = "CQ K1FM EM85";   // would be real frame bits in production
    ev.type      = 0;                 // JS8Call (any frame in message)
    ev.quality   = 0.42f;
    ev.mode      = 0;                 // JS8CallNormal

    DecodedText dt(ev);

    std::printf("frame:        %s\n", dt.frame().toUtf8());
    std::printf("message:      %s\n", dt.message().toUtf8());
    std::printf("frameType:    %u\n", static_cast<unsigned>(dt.frameType()));
    std::printf("isCompound:   %d\n", dt.isCompound());
    std::printf("isHeartbeat:  %d\n", dt.isHeartbeat());
    std::printf("isDirected:   %d\n", dt.isDirectedMessage());
    std::printf("snr:          %d\n", dt.snr());
    std::printf("submode:      %d\n", dt.submode());
    std::printf("freqOffset:   %d\n", dt.frequencyOffset());
    std::printf("string():     %s\n", dt.string().toUtf8());
    std::printf("messageWords:");
    for (const auto& w : dt.messageWords()) std::printf(" [%s]", w.toUtf8());
    std::printf("\n");
    return 0;
}

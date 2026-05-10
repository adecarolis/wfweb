// SPIKE-MINIMAL JS8.h — just the types DecodedText needs, no QObject machinery.
#pragma once
#include "QtGlobal"
#include "QString"
#include <array>
#include <string>
#include <variant>

namespace JS8 {

namespace Costas {
using Array = std::array<std::array<int, 7>, 3>;
}

namespace Event {

struct Decoded {
    int utc;
    int snr;
    float xdt;
    float frequency;
    std::string data;
    int type;
    float quality;
    int mode;
};

// Other event types unused by DecodedText, but defined so the variant compiles.
struct DecodeStarted {};
struct SyncStart {};
struct SyncState {};
struct DecodeFinished {};

using Variant =
    std::variant<DecodeStarted, SyncStart, SyncState, Decoded, DecodeFinished>;

} // namespace Event
} // namespace JS8

// SPIKE: extract just the JS8 encoder from JS8Call-improved/JS8_Mode/JS8.cpp
// to verify it compiles standalone (no Qt, no Boost, no FFTW, no Eigen).
//
// Source lines reused:
//   847..1053  — alphabet, alphabetWord, parity matrix lambdas
//   2776..2911 — encode() free function
//
// boost::augmented_crc is replaced with a hand-rolled CRC-12/0xc06 (≈ 20 lines).

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace JS8 {
namespace Costas {
using Array = std::array<std::array<int, 7>, 3>;
}

// ---------------- alphabet + alphabetWord ----------------

namespace {

constexpr std::string_view alphabet =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-+";

static_assert(alphabet.size() == 64);

constexpr auto alphabetWord = []() {
    constexpr std::uint8_t invalid = 0xff;

    constexpr auto words = []() {
        std::array<std::uint8_t, 256> words{};

        for (auto &word : words)
            word = invalid;

        for (std::size_t i = 0; i < alphabet.size(); ++i) {
            words[static_cast<std::uint8_t>(alphabet[i])] =
                static_cast<std::uint8_t>(i);
        }

        return words;
    }();

    return [words](char const value) {
        if (auto const word = words[static_cast<std::uint8_t>(value)];
            word != invalid) {
            return word;
        }
        throw std::runtime_error("Invalid character in JS8 message");
    };
}();

static_assert(alphabetWord('0') == 0);
static_assert(alphabetWord('A') == 10);
static_assert(alphabetWord('a') == 36);
static_assert(alphabetWord('-') == 62);
static_assert(alphabetWord('+') == 63);

// ---------------- CRC-12/0xc06 (replaces boost::augmented_crc) ----------------
//
// Augmented CRC: polynomial 0xc06, initial register 0, 12-bit width.
// "Augmented" means we treat the input as if it had 12 trailing zero bits
// (the future CRC slot) — this is what boost::augmented_crc does.
// XOR'd with 42 at the end to match the Fortran/JS8 convention.

template <typename T> std::uint16_t CRC12(T const &range) {
    std::uint32_t reg = 0;
    auto const *data = range.data();
    std::size_t const n = range.size();
    for (std::size_t i = 0; i < n; ++i) {
        reg ^= static_cast<std::uint32_t>(data[i]) << 4; // align byte to top of 12-bit reg
        for (int b = 0; b < 8; ++b) {
            if (reg & 0x800) reg = (reg << 1) ^ 0xc06;
            else             reg <<= 1;
            reg &= 0xfff;
        }
    }
    // Augmented: shift in 12 zero bits to "flush" the register
    for (int b = 0; b < 12; ++b) {
        if (reg & 0x800) reg = (reg << 1) ^ 0xc06;
        else             reg <<= 1;
        reg &= 0xfff;
    }
    return static_cast<std::uint16_t>(reg ^ 42);
}

// ---------------- parity matrix (87×87 LDPC) ----------------

constexpr auto parity = []() {
    constexpr std::size_t Rows = 87;
    constexpr std::size_t Cols = 87;

    using ElementType = std::uint64_t;
    constexpr std::size_t ElementSize =
        std::numeric_limits<ElementType>::digits;

    constexpr auto matrix = []() {
        constexpr std::array<std::string_view, Rows> Data = {
            "23bba830e23b6b6f50982e", "1f8e55da218c5df3309052",
            "ca7b3217cd92bd59a5ae20", "56f78313537d0f4382964e",
            "6be396b5e2e819e373340c", "293548a138858328af4210",
            "cb6c6afcdc28bb3f7c6e86", "3f2a86f5c5bd225c961150",
            "849dd2d63673481860f62c", "56cdaec6e7ae14b43feeee",
            "04ef5cfa3766ba778f45a4", "c525ae4bd4f627320a3974",
            "41fd9520b2e4abeb2f989c", "7fb36c24085a34d8c1dbc4",
            "40fc3e44bb7d2bb2756e44", "d38ab0a1d2e52a8ec3bc76",
            "3d0f929ef3949bd84d4734", "45d3814f504064f80549ae",
            "f14dbf263825d0bd04b05e", "db714f8f64e8ac7af1a76e",
            "8d0274de71e7c1a8055eb0", "51f81573dd4049b082de14",
            "d8f937f31822e57c562370", "b6537f417e61d1a7085336",
            "ecbd7c73b9cd34c3720c8a", "3d188ea477f6fa41317a4e",
            "1ac4672b549cd6dba79bcc", "a377253773ea678367c3f6",
            "0dbd816fba1543f721dc72", "ca4186dd44c3121565cf5c",
            "29c29dba9c545e267762fe", "1616d78018d0b4745ca0f2",
            "fe37802941d66dde02b99c", "a9fa8e50bcb032c85e3304",
            "83f640f1a48a8ebc0443ea", "3776af54ccfbae916afde6",
            "a8fc906976c35669e79ce0", "f08a91fb2e1f78290619a8",
            "cc9da55fe046d0cb3a770c", "d36d662a69ae24b74dcbd8",
            "40907b01280f03c0323946", "d037db825175d851f3af00",
            "1bf1490607c54032660ede", "0af7723161ec223080be86",
            "eca9afa0f6b01d92305edc", "7a8dec79a51e8ac5388022",
            "9059dfa2bb20ef7ef73ad4", "6abb212d9739dfc02580f2",
            "f6ad4824b87c80ebfce466", "d747bfc5fd65ef70fbd9bc",
            "612f63acc025b6ab476f7c", "05209a0abb530b9e7e34b0",
            "45b7ab6242b77474d9f11a", "6c280d2a0523d9c4bc5946",
            "f1627701a2d692fd9449e6", "8d9071b7e7a6a2eed6965e",
            "bf4f56e073271f6ab4bf80", "c0fc3ec4fb7d2bb2756644",
            "57da6d13cb96a7689b2790", "a9fa2eefa6f8796a355772",
            "164cc861bdd803c547f2ac", "cc6de59755420925f90ed2",
            "a0c0033a52ab6299802fd2", "b274db8abd3c6f396ea356",
            "97d4169cb33e7435718d90", "81cfc6f18c35b1e1f17114",
            "481a2a0df8a23583f82d6c", "081c29a10d468ccdbcecb6",
            "2c4142bf42b01e71076acc", "a6573f3dc8b16c9d19f746",
            "c87af9a5d5206abca532a8", "012dee2198eba82b19a1da",
            "b1ca4ea2e3d173bad4379c", "b33ec97be83ce413f9acc8",
            "5b0f7742bca86b8012609a", "37d8e0af9258b9e8c5f9b2",
            "35ad3fb0faeb5f1b0c30dc", "6114e08483043fd3f38a8a",
            "cd921fdf59e882683763f6", "95e45ecd0135aca9d6e6ae",
            "2e547dd7a05f6597aac516", "14cd0f642fc0c5fe3a65ca",
            "3a0a1dfd7eee29c2e827e0", "c8b5dffc335095dcdcaf2a",
            "3dd01a59d86310743ec752", "8abdb889efbe39a510a118",
            "3f231f212055371cf3e2a2"};

        constexpr std::size_t Total = (Rows * Cols + ElementSize - 1);
        constexpr std::size_t Count = Total / ElementSize;
        constexpr std::array<std::uint8_t, 4> Masks = {0x8, 0x4, 0x2, 0x1};

        std::array<ElementType, Count> data{};

        for (std::size_t row = 0; row < Rows; ++row) {
            std::size_t col = 0;

            for (auto const c : Data[row]) {
                std::uint8_t const value =
                    (c >= '0' && c <= '9')   ? c - '0'
                    : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                    : (c >= 'A' && c <= 'F') ? c - 'A' + 10
                                             : throw "Invalid hex";

                for (auto const mask : Masks) {
                    if (col >= Cols)
                        break;
                    if (value & mask) {
                        auto const index = row * Cols + col;
                        data[index / ElementSize] |=
                            ElementType{1} << (ElementSize - 1 - (index % ElementSize));
                    }
                    ++col;
                }
            }
        }

        return data;
    }();

    return [matrix](std::size_t row, std::size_t col) -> bool {
        std::size_t const index = row * Cols + col;
        return matrix[index / ElementSize] &
               (ElementType{1} << (ElementSize - 1 - (index % ElementSize)));
    };
}();

} // namespace

// ---------------- encode() — pasted verbatim from JS8.cpp:2776 ----------------

void encode(int const type, Costas::Array const &costas,
            const char *const message, int *const tones) {
    std::array<std::uint8_t, 11> bytes = {};

    for (int i = 0, j = 0; i < 12; i += 4, j += 3) {
        std::uint32_t words = (alphabetWord(message[i]) << 18) |
                              (alphabetWord(message[i + 1]) << 12) |
                              (alphabetWord(message[i + 2]) << 6) |
                              alphabetWord(message[i + 3]);
        bytes[j] = words >> 16;
        bytes[j + 1] = words >> 8;
        bytes[j + 2] = words;
    }

    bytes[9] = (type & 0b111) << 5;

    auto const crc = CRC12(bytes);
    bytes[9] |= (crc >> 7) & 0x1F;
    bytes[10] = (crc & 0x7F) << 1;

    auto costasData = tones;
    auto parityData = tones + 7;
    auto outputData = tones + 43;

    for (auto const &array : costas) {
        std::copy(array.begin(), array.end(), costasData);
        costasData += 36;
    }

    std::size_t outputBits = 0;
    std::size_t outputByte = 0;
    std::uint8_t outputMask = 0x80;
    std::uint8_t outputWord = 0;
    std::uint8_t parityWord = 0;

    for (std::size_t i = 0; i < 87; ++i) {
        std::size_t parityBits = 0;
        std::size_t parityByte = 0;
        std::uint8_t parityMask = 0x80;

        for (std::size_t j = 0; j < 87; ++j) {
            parityBits += parity(i, j) && (bytes[parityByte] & parityMask);
            parityMask =
                (parityMask == 1) ? (++parityByte, 0x80) : (parityMask >> 1);
        }

        parityWord = (parityWord << 1) | (parityBits & 1);
        outputWord =
            (outputWord << 1) | ((bytes[outputByte] & outputMask) != 0);
        outputMask =
            (outputMask == 1) ? (++outputByte, 0x80) : (outputMask >> 1);

        if (++outputBits == 3) {
            *parityData++ = parityWord;
            *outputData++ = outputWord;
            parityWord = 0;
            outputWord = 0;
            outputBits = 0;
        }
    }
}

// ---------------- C-callable wrappers exported to JS ----------------

constexpr Costas::Array kCostasOriginal = {{{4, 2, 5, 6, 1, 3, 0},
                                             {4, 2, 5, 6, 1, 3, 0},
                                             {4, 2, 5, 6, 1, 3, 0}}};

extern "C" {

// Encode a 12-character JS8 message of the given frame type (0..7).
// Output: 79 ints in `tones_out`, each 0..7 representing one 8-FSK symbol.
// Returns 0 on success, -1 on bad input character.
int js8_encode(int type, const char *message, int *tones_out) {
    try {
        encode(type, kCostasOriginal, message, tones_out);
        return 0;
    } catch (...) {
        return -1;
    }
}

} // extern "C"

} // namespace JS8

#pragma once

#include <QDebug>
#include <QLoggingCategory>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

Q_DECLARE_LOGGING_CATEGORY(decoder_js8);

namespace js8 {
/**
 * @brief Cache and combine repeated LLR frames for the same decode candidate.
 *
 * Uses a coarse freq/dt bin and a small LLR signature as the key; repeated
 * receptions accumulate LLRs to improve decode probability without changing
 * over-the-air behavior. Templated on the LLR length so the caller binds it
 * to the decoder's bit count.
 */
template <std::size_t N> class SoftCombiner {
    using Clock = std::chrono::steady_clock;

  public:
    struct Key {
        int mode;
        int freqBin;
        int dtBin;
        uint32_t signature;

        bool operator==(Key const &other) const noexcept {
            return mode == other.mode && freqBin == other.freqBin &&
                   dtBin == other.dtBin && signature == other.signature;
        }
    };

    struct Combined {
        Key key;
        std::array<float, N> llr0;
        std::array<float, N> llr1;
        int repeats;
        bool combined;
    };

    SoftCombiner() : SoftCombiner(defaultEnabled(), true) {}

    explicit SoftCombiner(bool enabled, bool runSelfTest = true)
        : m_enabled(enabled) {
        if (!m_enabled) {
            qCDebug(decoder_js8)
                << "soft-combining disabled (JS8_SOFT_COMBINING=0)";
        }
        if (runSelfTest)
            maybeRunSelfTest();
    }

    Key makeKey(int mode, float f1, float dt, std::array<float, N> const &llr0,
                std::array<float, N> const &llr1) const {
        return Key{mode, static_cast<int>(std::lround(f1)),
                   static_cast<int>(std::lround(dt * 10.0f)), // 100 ms bins
                   signature(llr0, llr1)};
    }

    Combined combine(Key const &key, std::array<float, N> const &llr0,
                     std::array<float, N> const &llr1,
                     std::chrono::seconds ttl) {
        flush(ttl);

        if (!m_enabled) {
            return Combined{key, llr0, llr1, 1, false};
        }

        auto &bucket = m_entries[keyForLookup(key)];
        auto it = findEntry(bucket, key.signature);

        if (it == bucket.end()) {
            bucket.push_back(makeEntry(key.signature, llr0, llr1));
            return Combined{key, llr0, llr1, 1, false};
        }

        for (std::size_t i = 0; i < llr0.size(); ++i) {
            it->llr0[i] += llr0[i];
            it->llr1[i] += llr1[i];
        }

        ++it->repeats;
        it->lastSeen = Clock::now();

        qCDebug(decoder_js8)
            << "soft-combining repeats" << it->repeats << "mode" << key.mode
            << "freq" << key.freqBin << "dtbin" << key.dtBin;

        return Combined{key, it->llr0, it->llr1, it->repeats, true};
    }

    void markDecoded(Key const &key) {
        if (!m_enabled)
            return;

        auto lookup = keyForLookup(key);
        auto it = m_entries.find(lookup);

        if (it == m_entries.end())
            return;

        auto &bucket = it->second;
        bucket.erase(std::remove_if(bucket.begin(), bucket.end(),
                                    [&key](Entry const &entry) {
                                        return entry.signature == key.signature;
                                    }),
                     bucket.end());

        if (bucket.empty())
            m_entries.erase(it);
    }

    void flush(std::chrono::seconds ttl) {
        if (!m_enabled)
            return;

        auto const now = Clock::now();

        for (auto it = m_entries.begin(); it != m_entries.end();) {
            auto &bucket = it->second;

            bucket.erase(std::remove_if(bucket.begin(), bucket.end(),
                                        [now, ttl](Entry const &entry) {
                                            return now - entry.lastSeen > ttl;
                                        }),
                         bucket.end());

            if (bucket.empty())
                it = m_entries.erase(it);
            else
                ++it;
        }
    }

  private:
    struct CoarseKey {
        int mode;
        int freqBin;
        int dtBin;

        bool operator==(CoarseKey const &other) const noexcept {
            return mode == other.mode && freqBin == other.freqBin &&
                   dtBin == other.dtBin;
        }
    };

    struct CoarseHash {
        std::size_t operator()(CoarseKey const &key) const noexcept {
            std::size_t const h1 = std::hash<int>{}(key.mode);
            std::size_t const h2 = std::hash<int>{}(key.freqBin);
            std::size_t const h3 = std::hash<int>{}(key.dtBin);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    struct Entry {
        uint32_t signature;
        std::array<float, N> llr0;
        std::array<float, N> llr1;
        int repeats;
        Clock::time_point lastSeen;
    };

    using Bucket = std::vector<Entry>;

    static constexpr auto signatureIndices() {
        std::array<int, 32> indices{};
        int value = 0;
        for (std::size_t i = 0; i < indices.size(); ++i) {
            value = (value + 37) % static_cast<int>(N);
            indices[i] = value;
        }
        return indices;
    }

    static typename Bucket::iterator findEntry(Bucket &bucket, uint32_t signature) {
        constexpr int MAX_HAMMING =
            4; // allow small differences between noisy repeats

        return std::find_if(
            bucket.begin(), bucket.end(), [signature](Entry const &entry) {
                return hamming(signature, entry.signature) <= MAX_HAMMING;
            });
    }

    static bool defaultEnabled() {
        bool ok = false;
        int value = qEnvironmentVariableIntValue("JS8_SOFT_COMBINING", &ok);
        return ok ? value != 0 : true;
    }

    static uint32_t signature(std::array<float, N> const &llr0,
                              std::array<float, N> const &llr1) {
        static constexpr auto INDICES = signatureIndices();

        uint32_t sig = 0;
        for (std::size_t i = 0; i < INDICES.size(); ++i) {
            auto const idx = INDICES[i];
            float const v = 0.5f * (llr0[idx] + llr1[idx]);
            if (v >= 0.0f)
                sig |= (1u << i);
        }
        return sig;
    }

    static int hamming(uint32_t a, uint32_t b) {
        uint32_t v = a ^ b;
        int c = 0;
        while (v) {
            v &= (v - 1);
            ++c;
        }
        return c;
    }

    static void maybeRunSelfTest() {
        static std::once_flag once;
        std::call_once(once, []() {
            if (!qEnvironmentVariableIsSet("JS8_SOFT_COMBINING_TEST"))
                return;

            SoftCombiner combiner(true, false);

            std::array<float, N> baseline{};
            for (std::size_t i = 0; i < baseline.size(); ++i) {
                baseline[i] = (i % 2 == 0) ? 2.0f : -2.0f;
            }

            auto noisy = [](std::array<float, N> base, int flipStride) {
                for (std::size_t i = 0; i < base.size(); ++i) {
                    if (i % flipStride == 0) {
                        base[i] *= -0.4f; // flip sign and reduce magnitude
                    } else {
                        base[i] *= 0.8f; // weaken but keep sign
                    }
                }
                return base;
            };

            auto llrA = noisy(baseline, 7);
            auto llrB = noisy(baseline, 11);

            auto key = combiner.makeKey(0, 1500.0f, 1.0f, llrA, llrB);
            auto first =
                combiner.combine(key, llrA, llrA, std::chrono::seconds{30});
            auto second =
                combiner.combine(key, llrB, llrB, std::chrono::seconds{30});

            auto countMatches = [](std::array<float, N> const &llr,
                                   std::array<float, N> const &reference) {
                int matches = 0;
                for (std::size_t i = 0; i < llr.size(); ++i) {
                    if (llr[i] * reference[i] > 0)
                        ++matches;
                }
                return matches;
            };

            int const matchesA = countMatches(first.llr0, baseline);
            int const matchesB = countMatches(llrB, baseline);
            int const matchesCombined = countMatches(second.llr0, baseline);

            qCDebug(decoder_js8)
                << "soft-combining self-test: A matches" << matchesA
                << "B matches" << matchesB << "combined matches"
                << matchesCombined << "repeats" << second.repeats;
        });
    }

    static Entry makeEntry(uint32_t signature, std::array<float, N> const &llr0,
                           std::array<float, N> const &llr1) {
        return Entry{signature, llr0, llr1, 1, Clock::now()};
    }

    CoarseKey keyForLookup(Key const &key) const {
        return CoarseKey{key.mode, key.freqBin, key.dtBin};
    }

    std::unordered_map<CoarseKey, Bucket, CoarseHash> m_entries;
    bool m_enabled;
};
} // namespace js8

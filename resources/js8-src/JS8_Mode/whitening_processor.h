#pragma once

#include <QDebug>
#include <QLoggingCategory>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <optional>
#include <sstream>
#include <vector>

Q_DECLARE_LOGGING_CATEGORY(decoder_js8);

namespace js8 {
/**
 * @brief Compute per-tone/symbol noise medians and whiten LLRs for a JS8 frame.
 *
 * Given symbol magnitudes (sans Costas) and winners, produces normalized
 * LLR0/LLR1, optionally applying noise-based whitening and erasure. Fully
 * templated on matrix dimensions, so it stays header-only; used inside the JS8
 * decoder per candidate.
 */
template <int NROWS, int ND, int N> class WhiteningProcessor {
  public:
    struct Result {
        std::array<float, 3 * ND> llr0;
        std::array<float, 3 * ND> llr1;
        bool whiteningApplied;
        bool erasureApplied;
        std::size_t erasures;
        double avgAbsPre;
        double avgAbsPost;
    };

    static Result process(std::array<std::array<float, ND>, NROWS> const &s1,
                          std::array<int, ND> const &symbolWinners,
                          float erasureThreshold, bool debug) {
        auto const median =
            [](std::vector<float> &values) -> std::optional<float> {
            if (values.empty())
                return std::nullopt;

            auto const mid = values.size() / 2;

            std::nth_element(values.begin(), values.begin() + mid,
                             values.end());
            float med = values[mid];

            if ((values.size() % 2) == 0 && mid > 0) {
                std::nth_element(values.begin(), values.begin() + (mid - 1),
                                 values.end());
                med = 0.5f * (med + values[mid - 1]);
            }

            return med;
        };

        // Estimate per-tone noise using non-winning tone magnitudes across the
        // frame.
        auto const toneNoise =
            [&]() -> std::optional<std::array<float, NROWS>> {
            std::array<std::vector<float>, NROWS> toneSamples;
            std::array<float, NROWS> noise = {};

            // Collect non-winning magnitudes for each tone.
            for (int j = 0; j < ND; ++j) {
                int const winner = symbolWinners[j];

                for (int i = 0; i < NROWS; ++i) {
                    if (i != winner)
                        toneSamples[i].push_back(s1[i][j]);
                }
            }

            bool ok = true;

            for (int i = 0; i < NROWS; ++i) {
                if (auto m = median(toneSamples[i]); m) {
                    noise[i] = *m;
                } else {
                    ok = false;
                    break;
                }
            }

            if (!ok)
                return std::nullopt;

            return noise;
        }();

        if (toneNoise && debug) {
            std::ostringstream oss;

            oss << "toneNoise:";
            for (auto const value : *toneNoise)
                oss << ' ' << value;

            qCDebug(decoder_js8).noquote() << oss.str().c_str();
        }

        // Estimate per-symbol noise using non-winning tone magnitudes per
        // symbol.
        auto const symbolNoise = [&]() -> std::optional<std::vector<float>> {
            std::vector<float> noise;
            noise.reserve(ND);

            for (int j = 0; j < ND; ++j) {
                std::vector<float> bins;
                bins.reserve(NROWS - 1);

                int const winner = symbolWinners[j];

                for (int i = 0; i < NROWS; ++i) {
                    if (i != winner)
                        bins.push_back(s1[i][j]);
                }

                if (auto m = median(bins); m) {
                    noise.push_back(*m);
                } else {
                    return std::nullopt;
                }
            }

            return noise;
        }();

        if (symbolNoise && !symbolNoise->empty() && debug) {
            auto const [minIt, maxIt] =
                std::minmax_element(symbolNoise->begin(), symbolNoise->end());
            float const avg = std::accumulate(symbolNoise->begin(),
                                              symbolNoise->end(), 0.0f) /
                              static_cast<float>(symbolNoise->size());

            qCDebug(decoder_js8)
                << "symbolNoise avg/min/max" << avg << *minIt << *maxIt;
        }

        Result result{};

        bool const disableWhitening =
            std::getenv("JS8_DISABLE_WHITENING") != nullptr;
        bool const whiteningAvailable = toneNoise && symbolNoise &&
                                        !symbolNoise->empty() &&
                                        !disableWhitening;
        bool const applyErasureInWhitening =
            whiteningAvailable && erasureThreshold > 0.0f;
        double sumAbsPre = 0.0;
        double sumAbsPost = 0.0;
        std::size_t erasures = 0;

        for (int j = 0; j < ND; ++j) {
            int const i1 = 3 * j;     // First column (matches Fortran's i1)
            int const i2 = 3 * j + 1; // Second column (matches Fortran's i2)
            int const i4 = 3 * j + 2; // Third column (matches Fortran's i4)

            std::array<float, NROWS> ps;

            for (int i = 0; i < NROWS; ++i)
                ps[i] = s1[i][j];

            // Assign to `bmeta` in column order, with correct values
            result.llr0[i1] = std::max({ps[4], ps[5], ps[6], ps[7]}) -
                              std::max({ps[0], ps[1], ps[2], ps[3]}); // r4
            result.llr0[i2] = std::max({ps[2], ps[3], ps[6], ps[7]}) -
                              std::max({ps[0], ps[1], ps[4], ps[5]}); // r2
            result.llr0[i4] = std::max({ps[1], ps[3], ps[5], ps[7]}) -
                              std::max({ps[0], ps[2], ps[4], ps[6]}); // r1

            for (auto &x : ps)
                x = std::log(x + 1e-32f);

            // Assign to `bmetb` in column order, with correct values
            result.llr1[i1] = std::max({ps[4], ps[5], ps[6], ps[7]}) -
                              std::max({ps[0], ps[1], ps[2], ps[3]}); // r4
            result.llr1[i2] = std::max({ps[2], ps[3], ps[6], ps[7]}) -
                              std::max({ps[0], ps[1], ps[4], ps[5]}); // r2
            result.llr1[i4] = std::max({ps[1], ps[3], ps[5], ps[7]}) -
                              std::max({ps[0], ps[2], ps[4], ps[6]}); // r1

            if (whiteningAvailable) {
                int const winner = symbolWinners[j];
                float const tn = std::max(0.0f, (*toneNoise)[winner]);
                float const sn = std::max(0.0f, (*symbolNoise)[j]);
                float const localNoise = std::sqrt(tn * sn + 1e-12f);

                auto const applyWhitening = [&](float &value) {
                    float const pre = std::abs(value);
                    sumAbsPre += pre;

                    if (localNoise > 0.0f && std::isfinite(localNoise)) {
                        value /= localNoise;
                    }

                    if (applyErasureInWhitening &&
                        std::abs(value) < erasureThreshold) {
                        value = 0.0f;
                        ++erasures;
                    }

                    sumAbsPost += std::abs(value);
                };

                applyWhitening(result.llr0[i1]);
                applyWhitening(result.llr0[i2]);
                applyWhitening(result.llr0[i4]);
                applyWhitening(result.llr1[i1]);
                applyWhitening(result.llr1[i2]);
                applyWhitening(result.llr1[i4]);
            }
        }

        auto const normalizeLLR = [](auto &llr) {
            float sum = 0.0f;
            float sum_of_squares = 0.0f;

            for (auto const value : llr) {
                sum += value;
                sum_of_squares += value * value;
            }

            float const llrav = sum / llr.size();
            float const llr2av = sum_of_squares / llr.size();
            float const variance = llr2av - llrav * llrav;
            float const llrsig = std::sqrt(variance > 0.0f ? variance : llr2av);

            for (float &val : llr)
                val = (val / llrsig) * 2.83f;
        };

        // Normalize and process metrics

        normalizeLLR(result.llr0);
        normalizeLLR(result.llr1);

        if (whiteningAvailable && debug) {
            auto const total =
                static_cast<double>(result.llr0.size() + result.llr1.size());
            double const avgPre = total > 0.0 ? sumAbsPre / total : 0.0;
            double const avgPost = total > 0.0 ? sumAbsPost / total : 0.0;

            qCDebug(decoder_js8) << "LLR whitening applied"
                                 << "avg|LLR| pre/post:" << avgPre << avgPost
                                 << "erasures:" << erasures;
        }

        result.whiteningApplied = whiteningAvailable;
        result.erasureApplied = applyErasureInWhitening;
        result.erasures = erasures;
        result.avgAbsPre = sumAbsPre;
        result.avgAbsPost = sumAbsPost;
        return result;
    }
};
} // namespace js8

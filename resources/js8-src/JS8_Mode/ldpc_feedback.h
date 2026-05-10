#pragma once

#include <QDebug>
#include <QLoggingCategory>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>

Q_DECLARE_LOGGING_CATEGORY(decoder_js8);

namespace js8 {
/**
 * @brief LDPC erasure threshold config and feedback refinement helpers.
 *
 * Inline env readers expose thresholds/pass limits; the templated
 * refineLlrsWithLdpcFeedback shrinks/boosts LLRs using the decoded
 * codeword to retry LDPC. Used inside the JS8 decode loop between
 * LDPC passes.
 */
constexpr float LLR_ERASURE_THRESHOLD_DEFAULT = 0.25f;
constexpr float LLR_FEEDBACK_CONFIDENT_MIN = 3.0f;
constexpr float LLR_FEEDBACK_UNCERTAIN_MAX = 1.0f;
constexpr float LLR_FEEDBACK_CONFIDENT_BOOST = 1.2f;
constexpr float LLR_FEEDBACK_UNCERTAIN_SHRINK = 0.5f;
constexpr float LLR_FEEDBACK_MAX_MAG = 6.0f;
constexpr int LDPC_FEEDBACK_MAX_PASSES_DEFAULT = 8;

inline float llrErasureThreshold() {
    float threshold = LLR_ERASURE_THRESHOLD_DEFAULT;

    if (auto const env = std::getenv("JS8_LLR_ERASURE_THRESH"); env) {
        char *end = nullptr;
        float val = std::strtof(env, &end);

        if (end != env && std::isfinite(val)) {
            threshold = val;
        }
    }

    if (threshold <= 0.0f || !std::isfinite(threshold) ||
        std::getenv("JS8_DISABLE_ERASURE_THRESHOLDING")) {
        return 0.0f;
    }

    return threshold;
}

inline bool ldpcFeedbackEnabled() {
    bool ok = false;
    int value = qEnvironmentVariableIntValue("JS8_LDPC_FEEDBACK", &ok);
    return ok ? value != 0 : true;
}

inline int ldpcFeedbackMaxPasses() {
    bool ok = false;
    int value = qEnvironmentVariableIntValue("JS8_LDPC_MAX_PASSES", &ok);

    if (!ok)
        return LDPC_FEEDBACK_MAX_PASSES_DEFAULT;

    return std::clamp(value, 1, LDPC_FEEDBACK_MAX_PASSES_DEFAULT);
}

template <std::size_t N>
void refineLlrsWithLdpcFeedback(std::array<float, N> const &llrIn,
                                std::array<int8_t, N> const &cw,
                                float erasureThreshold,
                                std::array<float, N> &llrOut,
                                int &confidentCount, int &uncertainCount) {
    llrOut = llrIn;
    confidentCount = 0;
    uncertainCount = 0;

    for (std::size_t i = 0; i < llrOut.size(); ++i) {
        float &value = llrOut[i];

        if (!std::isfinite(value)) {
            value = 0.0f;
            ++uncertainCount;
            continue;
        }

        bool const bitOne = cw[i] != 0;
        float const mag = std::abs(value);
        bool const signMatch = (value >= 0.0f) == bitOne;

        if (signMatch && mag >= LLR_FEEDBACK_CONFIDENT_MIN) {
            ++confidentCount;
            float boosted = mag * LLR_FEEDBACK_CONFIDENT_BOOST;
            boosted = std::clamp(boosted, 0.0f, LLR_FEEDBACK_MAX_MAG);
            value = bitOne ? boosted : -boosted;
        } else if (!signMatch || mag <= LLR_FEEDBACK_UNCERTAIN_MAX) {
            ++uncertainCount;
            float shrunk = mag * LLR_FEEDBACK_UNCERTAIN_SHRINK;

            if (erasureThreshold > 0.0f && shrunk < erasureThreshold) {
                value = 0.0f;
            } else {
                value = bitOne ? shrunk : -shrunk;
            }
        }
    }
}
} // namespace js8

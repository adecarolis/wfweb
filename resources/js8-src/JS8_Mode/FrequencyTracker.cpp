/**
 * @file FrequencyTracker.cpp
 * @brief Implementation of Kalman filter-based trackers
 */
#include "FrequencyTracker.h"

#include <algorithm>
#include <cmath>

/**
 * @brief JS8 namespace for Kalman filter-based trackers
 *
 */
namespace js8 {
/**
 * @brief Reset the FrequencyTracker with initial parameters
 *
 * @param initial_hz Initial frequency estimate in Hz
 * @param sample_rate_hz Sample rate in Hz
 * @param alpha Smoothing factor
 * @param max_step_hz Maximum step size in Hz
 * @param max_error_hz Maximum allowable error in Hz
 */
void FrequencyTracker::reset(double initial_hz, double sample_rate_hz,
                             double alpha, double max_step_hz,
                             double max_error_hz) {
    m_enabled = true;
    m_est_hz = initial_hz;
    m_fs = sample_rate_hz;
    m_alpha = alpha;
    m_max_step_hz = max_step_hz;
    m_max_error_hz = max_error_hz;
    m_sum_abs = 0.0;
    m_updates = 0;
}

/**
 * @brief Disable the FrequencyTracker
 *
 */
void FrequencyTracker::disable() { m_enabled = false; }

/**
 * @brief Check if the FrequencyTracker is enabled
 *
 * @return true
 * @return false
 */
bool FrequencyTracker::enabled() const noexcept { return m_enabled; }

/**
 * @brief Get the current frequency estimate in Hz
 *
 * @return double
 */
double FrequencyTracker::currentHz() const noexcept { return m_est_hz; }

/**
 * @brief Get the average step size in Hz
 *
 * @return double
 */
double FrequencyTracker::averageStepHz() const noexcept {
    return m_updates > 0 ? m_sum_abs / static_cast<double>(m_updates) : 0.0;
}

/**
 * @brief Apply frequency correction to the provided data
 *
 * @param data Pointer to complex float data
 * @param count Number of samples
 */
void FrequencyTracker::apply(std::complex<float> *data, int count) const {
    if (!m_enabled || !data || count <= 0 || m_fs <= 0.0)
        return;

    double const dphi = 2.0 * std::numbers::pi * (m_est_hz / m_fs);
    auto const wstep = std::polar(1.0f, static_cast<float>(dphi));
    auto w = std::complex<float>{1.0f, 0.0f};

    for (int i = 0; i < count; ++i) {
        w *= wstep;
        data[i] *= w;
    }
}

/**
 * @brief Update the FrequencyTracker with a new residual frequency measurement
 *
 * @param residual_hz Residual frequency in Hz
 * @param weight Weighting factor
 */
void FrequencyTracker::update(double residual_hz, double weight) {
    if (!m_enabled || m_fs <= 0.0)
        return;
    if (!std::isfinite(residual_hz) || !std::isfinite(weight) || weight <= 0.0)
        return;
    if (std::abs(residual_hz) > m_max_error_hz)
        return;

    residual_hz *= std::min(weight, 1.0);

    double const step = std::clamp(residual_hz, -m_max_step_hz, m_max_step_hz);
    m_est_hz += m_alpha * step;

    m_sum_abs += std::abs(step);
    ++m_updates;
}

/**
 * @brief Reset the TimingTracker with initial parameters
 *
 * @param initial_samples Initial timing estimate in samples
 * @param alpha Smoothing factor
 * @param max_step Maximum step size in samples
 * @param max_total_error Maximum allowable total error in samples
 */
void TimingTracker::reset(double initial_samples, double alpha, double max_step,
                          double max_total_error) {
    m_enabled = true;
    m_est_samples = initial_samples;
    m_alpha = alpha;
    m_max_step = max_step;
    m_max_total = max_total_error;
    m_sum_abs = 0.0;
    m_updates = 0;
}

/**
 * @brief Disable the TimingTracker
 *
 */
void TimingTracker::disable() { m_enabled = false; }

/**
 * @brief Check if the TimingTracker is enabled
 *
 * @return true
 * @return false
 */
bool TimingTracker::enabled() const noexcept { return m_enabled; }

/**
 * @brief Get the current timing estimate in samples
 *
 * @return double
 */
double TimingTracker::currentSamples() const noexcept { return m_est_samples; }

/**
 * @brief Get the average step size in samples
 *
 * @return double
 */
double TimingTracker::averageStepSamples() const noexcept {
    return m_updates > 0 ? m_sum_abs / static_cast<double>(m_updates) : 0.0;
}

/**
 * @brief Update the TimingTracker with a new residual timing measurement
 *
 * @param residual_samples Residual timing in samples
 * @param weight Weighting factor
 */
void TimingTracker::update(double residual_samples, double weight) {
    if (!m_enabled)
        return;
    if (!std::isfinite(residual_samples) || !std::isfinite(weight) ||
        weight <= 0.0)
        return;

    residual_samples *= std::min(weight, 1.0);

    double const step = std::clamp(residual_samples, -m_max_step, m_max_step);
    double const next = m_est_samples + m_alpha * step;

    if (std::abs(next) > m_max_total)
        return;

    m_est_samples = next;
    m_sum_abs += std::abs(step);
    ++m_updates;
}
} // namespace js8

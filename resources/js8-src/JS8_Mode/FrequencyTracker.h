#pragma once

#include <complex>
#include <numbers>

namespace js8 {
/**
 * @brief Lightweight PLL/Kalman-style tracker for residual frequency offset.
 *
 * Initialized with coarse estimates and sample rate; apply() rotates samples
 * by the tracked offset, update() nudges the estimate using pilot residuals.
 * Used inside the JS8 decode loop per candidate frame.
 */
class FrequencyTracker {
  public:
    void reset(double initial_hz, double sample_rate_hz, double alpha = 0.15,
               double max_step_hz = 0.3, double max_error_hz = 5.0);

    void disable();

    [[nodiscard]] bool enabled() const noexcept;

    [[nodiscard]] double currentHz() const noexcept;

    [[nodiscard]] double averageStepHz() const noexcept;

    void apply(std::complex<float> *data, int count) const;

    void update(double residual_hz, double weight = 1.0);

  private:
    bool m_enabled = true;
    double m_est_hz = 0.0;
    double m_fs = 0.0;
    double m_alpha = 0.15;
    double m_max_step_hz = 0.3;
    double m_max_error_hz = 5.0;
    double m_sum_abs = 0.0;
    int m_updates = 0;
};

class TimingTracker {
  public:
    /**
     * @brief Tracks residual timing (sample) offset between the symbol clock
     * and the signal.
     *
     * Initialized with bounds and step limits; update() ingests early/late
     * energy errors from pilots to refine sample alignment. Used per candidate
     * in the JS8 decode loop alongside FrequencyTracker.
     */

    void reset(double initial_samples, double alpha = 0.15,
               double max_step = 0.35, double max_total_error = 2.0);

    void disable();

    [[nodiscard]] bool enabled() const noexcept;

    [[nodiscard]] double currentSamples() const noexcept;

    [[nodiscard]] double averageStepSamples() const noexcept;

    void update(double residual_samples, double weight = 1.0);

  private:
    bool m_enabled = true;
    double m_est_samples = 0.0;
    double m_alpha = 0.15;
    double m_max_step = 0.35;
    double m_max_total = 2.0;
    double m_sum_abs = 0.0;
    int m_updates = 0;
};
} // namespace js8

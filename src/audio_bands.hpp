// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <cstddef>

namespace ren {
// Overlapping low/mid/high bands for the indicator, at the capture rate of 16 kHz.
// Four one-pole filters avoid an FFT and never alter the recorded samples.
class AudioBands {
    std::array<double, 4> low_{};
    std::array<double, 4> alpha_{};
    std::array<double, 3> energy_{};
    std::size_t count_ = 0;
public:
    AudioBands() {
        constexpr std::array<double, 4> cutoffs{80, 350, 1400, 5000};
        for (std::size_t i = 0; i < 4; ++i)
            alpha_[i] = 1 - std::exp(-2 * 3.141592653589793 * cutoffs[i] / 16000);
    }
    void add(double sample) {
        for (std::size_t i = 0; i < 4; ++i)
            low_[i] += alpha_[i] * (sample - low_[i]);
        for (std::size_t i = 0; i < 3; ++i) {
            const double band = low_[i + 1] - low_[i];
            energy_[i] += band * band;
        }
        ++count_;
    }
    std::array<double, 3> take() {
        auto result = energy_;
        for (auto& value : result) value = count_ ? std::sqrt(value / count_) : 0;
        energy_ = {};
        count_ = 0;
        return result;
    }
};
}

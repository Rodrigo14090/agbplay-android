#include "Resampler.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

static std::vector<float> Render(Resampler &resampler, float ratio) {
    size_t position = 0;
    auto fetch = [&](std::vector<float> &buffer, size_t required) {
        while (buffer.size() < required) {
            // Deterministic mixture below Nyquist, with no endpoint discontinuity.
            const double t = double(position++);
            buffer.push_back(float(0.6 * std::sin(t * 0.03) + 0.2 * std::cos(t * 0.11)));
        }
        return true;
    };
    std::vector<float> result(8192);
    resampler.Process(result, ratio, fetch);
    return result;
}
int main() {
    for (auto type : {ResamplerType::SINC, ResamplerType::BLEP, ResamplerType::BLAMP}) {
        for (float ratio : {0.5f, 1.0f, 1.7f, 4.2f}) {
            auto selected = Resampler::MakeResampler(type);
            std::unique_ptr<Resampler> scalar;
            if (type == ResamplerType::SINC) scalar = std::make_unique<SincResampler>();
            if (type == ResamplerType::BLEP) scalar = std::make_unique<BlepResampler>();
            if (type == ResamplerType::BLAMP) scalar = std::make_unique<BlampResampler>();
            const auto actual = Render(*selected, ratio);
            const auto reference = Render(*scalar, ratio);
            double maxError = 0;
            for (size_t i = 0; i < actual.size(); ++i) {
                if (!std::isfinite(actual[i])) throw std::runtime_error("Nonfinite resampler output");
                maxError = std::max(maxError, double(std::abs(actual[i] - reference[i])));
            }
            std::cout << "type=" << int(type) << " ratio=" << ratio << " max selected/scalar error=" << maxError << '\n';
            if (maxError > 0.001) throw std::runtime_error("Resampler parity exceeded tolerance");
        }
    }
}

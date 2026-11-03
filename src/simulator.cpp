#include "simulator.hpp"
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>

namespace queue_control {
SimulationResult simulate(const Model& model, int cap, int initial_bid, int initial_ask,
                          const SimulationOptions& options) {
    validate_start(initial_bid, initial_ask, cap);
    validate_model(model, cap);
    if (options.runs < 2 || options.runs > 10000000 ||
        options.event_limit < 1 || options.event_limit > 100000000)
        throw std::invalid_argument("runs must be in [2, 10000000]; event limit in [1, 100000000]");
    std::mt19937_64 generator(options.seed);
    SimulationResult result{options.runs, 0, std::nullopt};
    std::uint64_t ask_first = 0;
    double mean = 0, sum_squared_deviations = 0;
    for (std::uint64_t path = 0; path < options.runs; ++path) {
        int bid = initial_bid, ask = initial_ask;
        double elapsed = 0;
        std::uint64_t events = 0;
        while (bid > 0 && ask > 0) {
            if (events == options.event_limit) return result;
            const auto rates = model.rates(bid, ask, cap);
            const double event_rates[] = {rates.bid_addition, rates.bid_removal,
                                          rates.ask_addition, rates.ask_removal};
            double next_time = std::numeric_limits<double>::infinity();
            int next_event = -1;
            for (int event = 0; event < 4; ++event) {
                if (event_rates[event] == 0) continue;
                std::exponential_distribution<double> clock(event_rates[event]);
                const double waiting = clock(generator);
                if (!std::isfinite(waiting) || waiting < 0)
                    throw std::runtime_error("nonfinite or invalid simulation clock");
                if (waiting < next_time) { next_time = waiting; next_event = event; }
            }
            elapsed += next_time;
            if (!std::isfinite(elapsed)) throw std::runtime_error("nonfinite path duration");
            switch (next_event) {
                case 0: ++bid; break;
                case 1: --bid; break;
                case 2: ++ask; break;
                case 3: --ask; break;
                default: throw std::runtime_error("no enabled simulation event");
            }
            ++events;
        }
        ++result.completed;
        if (ask == 0) ++ask_first;
        const double delta = elapsed - mean;
        mean += delta / static_cast<double>(result.completed);
        sum_squared_deviations += delta * (elapsed - mean);
        if (!std::isfinite(mean) || !std::isfinite(sum_squared_deviations))
            throw std::runtime_error("nonfinite duration statistics");
    }
    const double n = static_cast<double>(result.completed);
    const double frequency = static_cast<double>(ask_first) / n;
    constexpr double z = 1.959963984540054;
    const double denominator = 1 + z * z / n;
    const double center = (frequency + z * z / (2 * n)) / denominator;
    const double half_width = z * std::sqrt(frequency * (1 - frequency) / n +
                                           z * z / (4 * n * n)) / denominator;
    const double stddev = std::sqrt(sum_squared_deviations / (n - 1));
    const double stderr = stddev / std::sqrt(n);
    result.estimates = Estimates{frequency, {center - half_width, center + half_width},
        mean, stddev, stderr, {mean - z * stderr, mean + z * stderr}};
    return result;
}
} // namespace queue_control

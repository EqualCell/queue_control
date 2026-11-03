#pragma once
#include "model.hpp"
#include <cstdint>
#include <optional>

namespace queue_control {
struct SimulationOptions {
    std::uint64_t runs = 100000;
    std::uint64_t seed = 42;
    std::uint64_t event_limit = 1000000;
};
struct Interval { double lower, upper; };
struct Estimates {
    double ask_first_frequency;
    Interval probability_interval;
    double mean_seconds, duration_stddev, duration_stderr;
    Interval duration_interval;
};
struct SimulationResult {
    std::uint64_t requested, completed = 0;
    // No estimates are exposed for an incomplete experiment.
    std::optional<Estimates> estimates;
};
SimulationResult simulate(const Model& model, int cap, int bid, int ask,
                          const SimulationOptions& options = {});
} // namespace queue_control

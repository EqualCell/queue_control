#pragma once
#include "model.hpp"
#include <cstddef>
#include <vector>

namespace queue_control {
struct SolverOptions {
    double probability_tolerance = 1e-11;
    double duration_tolerance = 1e-10; // seconds
    std::size_t max_iterations = 100000;
};
struct Diagnostics {
    bool converged = false;
    std::size_t iterations = 0;
    double max_residual = 0;
};
struct Solution {
    int cap;
    std::vector<double> probability;
    std::vector<double> duration;
    Diagnostics probability_diagnostics, duration_diagnostics;
    double p(int bid, int ask) const;
    double seconds(int bid, int ask) const;
    bool converged() const;
};
Solution solve(const Model& model, int cap, const SolverOptions& options = {});
} // namespace queue_control

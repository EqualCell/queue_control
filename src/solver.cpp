#include "solver.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace queue_control {
namespace {
std::size_t index(int bid, int ask, int cap) {
    return static_cast<std::size_t>(bid) * (cap + 1) + ask;
}
void validate_grid_state(int bid, int ask, int cap) {
    if (bid < 0 || ask < 0 || bid > cap || ask > cap || (bid == 0 && ask == 0))
        throw std::invalid_argument("invalid grid state; (0,0) has no boundary value");
}
// Normalize each rate before multiplying, avoiding unnecessary product overflow.
double first_step(const std::vector<double>& grid, const Rates& rate,
                  int bid, int ask, int cap, bool duration) {
    const double total = rate.total();
    double value = duration ? 1.0 / total : 0.0;
    value += (rate.bid_removal / total) * grid[index(bid - 1, ask, cap)];
    value += (rate.ask_removal / total) * grid[index(bid, ask - 1, cap)];
    if (bid < cap && rate.bid_addition > 0)
        value += (rate.bid_addition / total) * grid[index(bid + 1, ask, cap)];
    if (ask < cap && rate.ask_addition > 0)
        value += (rate.ask_addition / total) * grid[index(bid, ask + 1, cap)];
    if (!std::isfinite(value))
        throw std::runtime_error("nonfinite solver result");
    return value;
}
Diagnostics iterate(std::vector<double>& grid, const std::vector<Rates>& rates,
                    int cap, bool duration, double tolerance, std::size_t limit) {
    Diagnostics diagnostics;
    for (std::size_t sweep = 0; sweep < limit; ++sweep) {
        for (int bid = 1; bid <= cap; ++bid)
            for (int ask = 1; ask <= cap; ++ask) {
                const auto i = index(bid, ask, cap);
                grid[i] = first_step(grid, rates[i], bid, ask, cap, duration);
            }
        double residual = 0;
        for (int bid = 1; bid <= cap; ++bid)
            for (int ask = 1; ask <= cap; ++ask) {
                const auto i = index(bid, ask, cap);
                residual = std::max(residual, std::abs(grid[i] -
                    first_step(grid, rates[i], bid, ask, cap, duration)));
            }
        diagnostics = {residual <= tolerance, sweep + 1, residual};
        if (diagnostics.converged) break;
    }
    return diagnostics;
}
} // namespace

double Solution::p(int bid, int ask) const {
    validate_grid_state(bid, ask, cap);
    return probability.at(index(bid, ask, cap));
}
double Solution::seconds(int bid, int ask) const {
    validate_grid_state(bid, ask, cap);
    return duration.at(index(bid, ask, cap));
}
bool Solution::converged() const {
    return probability_diagnostics.converged && duration_diagnostics.converged;
}
Solution solve(const Model& model, int cap, const SolverOptions& options) {
    validate_model(model, cap);
    if (!std::isfinite(options.probability_tolerance) || options.probability_tolerance <= 0 ||
        !std::isfinite(options.duration_tolerance) || options.duration_tolerance <= 0 ||
        options.max_iterations == 0 || options.max_iterations > 1000000)
        throw std::invalid_argument("solver tolerances must be finite and positive; iterations in [1, 1000000]");
    const auto size = static_cast<std::size_t>(cap + 1) * (cap + 1);
    Solution solution{cap, std::vector<double>(size, 0), std::vector<double>(size, 0), {}, {}};
    // The unused corner is explicitly undefined, and never read by the stencil.
    solution.probability[0] = solution.duration[0] = std::numeric_limits<double>::quiet_NaN();
    for (int bid = 1; bid <= cap; ++bid) solution.probability[index(bid, 0, cap)] = 1;
    std::vector<Rates> rates(size);
    for (int bid = 1; bid <= cap; ++bid)
        for (int ask = 1; ask <= cap; ++ask)
            rates[index(bid, ask, cap)] = model.rates(bid, ask, cap);
    solution.probability_diagnostics = iterate(solution.probability, rates, cap, false,
        options.probability_tolerance, options.max_iterations);
    solution.duration_diagnostics = iterate(solution.duration, rates, cap, true,
        options.duration_tolerance, options.max_iterations);
    return solution;
}
} // namespace queue_control

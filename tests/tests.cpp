#include "model.hpp"
#include "solver.hpp"
#include "simulator.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
using namespace queue_control;
namespace {
int checks = 0;
void require(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, double tolerance, const std::string& message) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
void rejects(const std::function<void()>& action, const std::string& message) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
}
Model depletion(double bid_rate = 1, double ask_rate = 1) {
    Model model;
    model.bid_addition = model.ask_addition = 0;
    model.bid_removal_intercept = bid_rate;
    model.ask_removal_intercept = ask_rate;
    return model;
}
void deterministic() {
    const auto race = solve(depletion(1, 3), 1);
    near(race.p(1, 1), .75, 1e-13, "one-unit probability");
    near(race.seconds(1, 1), .25, 1e-13, "one-unit duration");
    const auto pure = solve(depletion(), 2);
    near(pure.p(2, 1), .75, 1e-13, "pure (2,1) probability");
    near(pure.p(1, 2), .25, 1e-13, "pure (1,2) probability");
    near(pure.p(2, 2), .5, 1e-13, "pure (2,2) probability");
    near(pure.seconds(2, 1), .75, 1e-13, "pure (2,1) duration");
    near(pure.seconds(1, 2), .75, 1e-13, "pure (1,2) duration");
    near(pure.seconds(2, 2), 1.25, 1e-13, "pure (2,2) duration");
    SolverOptions precise{1e-13, 1e-12, 100000};
    for (auto model : {constant_model(), linear_model()}) {
        const auto solution = solve(model, 8, precise);
        require(solution.converged(), "symmetric convergence");
        double probability_residual = 0, duration_residual = 0;
        for (int b = 1; b <= 8; ++b) {
            near(solution.p(b, 0), 1, 0, "ask boundary");
            near(solution.p(0, b), 0, 0, "bid boundary");
            near(solution.seconds(b, 0), 0, 0, "ask duration boundary");
            near(solution.seconds(0, b), 0, 0, "bid duration boundary");
            near(solution.p(b, b), .5, 1e-10, "diagonal symmetry");
            for (int a = 1; a <= 8; ++a) {
                near(solution.p(b, a) + solution.p(a, b), 1, 1e-10, "probability symmetry");
                near(solution.seconds(b, a), solution.seconds(a, b), 1e-9, "duration symmetry");
                require(std::isfinite(solution.p(b, a)) && solution.p(b, a) >= -1e-12 &&
                        solution.p(b, a) <= 1 + 1e-12, "probability range");
                require(std::isfinite(solution.seconds(b, a)) && solution.seconds(b, a) >= 0, "duration range");
                const auto r = model.rates(b, a, 8);
                double p = r.bid_removal * solution.p(b - 1, a) + r.ask_removal * solution.p(b, a - 1);
                double w = 1 + r.bid_removal * solution.seconds(b - 1, a) + r.ask_removal * solution.seconds(b, a - 1);
                if (b < 8) { p += r.bid_addition * solution.p(b + 1, a); w += r.bid_addition * solution.seconds(b + 1, a); }
                if (a < 8) { p += r.ask_addition * solution.p(b, a + 1); w += r.ask_addition * solution.seconds(b, a + 1); }
                probability_residual = std::max(probability_residual, std::abs(solution.p(b, a) - p / r.total()));
                duration_residual = std::max(duration_residual, std::abs(solution.seconds(b, a) - w / r.total()));
            }
        }
        require(probability_residual <= 1.01e-13 && duration_residual <= 1.01e-12, "independent residual checks");
        near(solution.probability_diagnostics.max_residual, probability_residual, 1e-15, "reported probability residual");
        near(solution.duration_diagnostics.max_residual, duration_residual, 1e-14, "reported duration residual");
        rejects([&] { solution.p(0, 0); }, "undefined corner probability");
        rejects([&] { solution.seconds(0, 0); }, "undefined corner duration");
        for (double scale : {.25, 4.0}) {
            Model scaled = model;
            scaled.bid_addition *= scale; scaled.ask_addition *= scale;
            scaled.bid_removal_intercept *= scale; scaled.ask_removal_intercept *= scale;
            scaled.bid_removal_slope *= scale; scaled.ask_removal_slope *= scale;
            const auto other = solve(scaled, 8, precise);
            require(other.converged(), "scaled convergence");
            for (int b = 1; b <= 8; ++b) for (int a = 1; a <= 8; ++a) {
                near(other.p(b, a), solution.p(b, a), 1e-10, "scaled probability");
                near(other.seconds(b, a), solution.seconds(b, a) / scale, 1e-9, "scaled duration");
            }
        }
    }
    auto capped = depletion(1, 3);
    capped.bid_addition = 100; capped.ask_addition = 200;
    const auto cap_rates = capped.rates(1, 1, 1);
    near(cap_rates.total(), 4, 0, "disabled additions excluded from total");
    const auto cap_solution = solve(capped, 1);
    near(cap_solution.p(1, 1), .75, 1e-13, "cap probability");
    near(cap_solution.seconds(1, 1), .25, 1e-13, "cap duration");
    near(capped.rates(2, 1, 2).bid_addition, 0, 0, "bid cap");
    near(capped.rates(1, 2, 2).ask_addition, 0, 0, "ask cap");
    require(!solve(constant_model(), 8, {1e-14, 1e-14, 1}).converged(), "iteration limit reported");
    for (int cap : {0, -1, 101, std::numeric_limits<int>::max()})
        rejects([&] { solve(constant_model(), cap); }, "invalid cap");
    for (auto state : {std::pair<int,int>{0, 1}, {1, 0}, {-1, 1}, {3, 1}, {1, 3}})
        rejects([&] { simulate(constant_model(), 2, state.first, state.second, {2, 42, 10}); }, "invalid start");
    for (double invalid : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto model = constant_model(); model.bid_addition = invalid;
        rejects([&] { solve(model, 1); }, "invalid nominal addition even at cap");
        model = constant_model(); model.ask_removal_intercept = invalid;
        rejects([&] { solve(model, 2); }, "invalid removal");
    }
    auto invalid = constant_model(); invalid.bid_removal_intercept = 0;
    rejects([&] { solve(invalid, 2); }, "zero removal");
    invalid = constant_model(); invalid.bid_removal_slope = -.6;
    rejects([&] { solve(invalid, 2); }, "validate all states");
    invalid = constant_model(); invalid.bid_addition = invalid.ask_addition = std::numeric_limits<double>::max();
    rejects([&] { solve(invalid, 2); }, "total overflow");
    rejects([&] { solve(constant_model(), 2, {0, 1e-10, 10}); }, "invalid tolerance");
    rejects([&] { solve(constant_model(), 2, {1e-10, 1e-10, 0}); }, "invalid iteration count");
    rejects([&] { solve(depletion(1e-310, 1e-310), 1); }, "nonfinite solver duration");
    for (SimulationOptions options : {SimulationOptions{1, 42, 10}, {10000001, 42, 10}, {2, 42, 0}, {2, 42, 100000001}})
        rejects([&] { simulate(constant_model(), 2, 1, 1, options); }, "invalid simulation size");
    const auto incomplete = simulate(depletion(), 2, 2, 2, {10, 42, 1});
    require(incomplete.requested == 10 && incomplete.completed == 0 && !incomplete.estimates, "incomplete experiment");
    const auto complete = simulate(depletion(), 1, 1, 1, {10, 42, 1});
    require(complete.completed == 10 && complete.estimates.has_value(), "absorption at exact event limit");
    const auto first = simulate(linear_model(), 5, 2, 3, {1000, 1234, 10000});
    const auto second = simulate(linear_model(), 5, 2, 3, {1000, 1234, 10000});
    require(first.estimates && second.estimates, "repeatability experiments complete");
    near(first.estimates->ask_first_frequency, second.estimates->ask_first_frequency, 0, "repeatable probability");
    near(first.estimates->mean_seconds, second.estimates->mean_seconds, 0, "repeatable mean");
    near(first.estimates->duration_stddev, second.estimates->duration_stddev, 0, "repeatable SD");
    const auto capped_mc = simulate(capped, 1, 1, 1, {1000, 42, 1});
    const auto pure_mc = simulate(depletion(1, 3), 1, 1, 1, {1000, 42, 1});
    near(capped_mc.estimates->mean_seconds, pure_mc.estimates->mean_seconds, 0, "cap clocks omit additions");
    near(capped_mc.estimates->ask_first_frequency, pure_mc.estimates->ask_first_frequency, 0, "cap outcome equivalence");
}
void statistical_check(const Model& model, int cap, int b, int a, double p, double w, std::uint64_t seed) {
    const auto result = simulate(model, cap, b, a, {50000, seed, 1000000});
    require(result.completed == result.requested && result.estimates.has_value(), "statistical completion");
    const auto& e = *result.estimates;
    const double probability_se = std::sqrt(p * (1 - p) / static_cast<double>(result.completed));
    near(e.ask_first_frequency, p, 6 * probability_se + 1e-9, "statistical probability within 6 SE");
    near(e.mean_seconds, w, 6 * e.duration_stderr + 1e-8, "statistical duration within 6 SE");
    require(e.probability_interval.lower <= e.ask_first_frequency && e.probability_interval.upper >= e.ask_first_frequency,
            "Wilson interval contains estimate");
    near(e.duration_stderr, e.duration_stddev / std::sqrt(50000.0), 1e-15, "duration SE");
    std::cout << "seed=" << seed << " state=(" << b << ',' << a << ") p=" << e.ask_first_frequency
              << " reference=" << p << " seconds=" << e.mean_seconds << " reference=" << w << '\n';
}
void statistical() {
    statistical_check(depletion(1, 3), 1, 1, 1, .75, .25, 1001);
    statistical_check(depletion(), 2, 2, 1, .75, .75, 1002);
    statistical_check(depletion(), 2, 1, 2, .25, .75, 1003);
    statistical_check(depletion(), 2, 2, 2, .5, 1.25, 1004);
    for (auto model : {constant_model(), linear_model()}) {
        auto solution = solve(model, 8);
        require(solution.converged(), "statistical reference converged");
        statistical_check(model, 8, 3, 5, solution.p(3, 5), solution.seconds(3, 5), 1005);
    }
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("specify deterministic or statistical");
        const std::string mode = argv[1];
        if (mode == "deterministic") deterministic();
        else if (mode == "statistical") statistical();
        else throw std::runtime_error("unknown test mode");
        std::cout << "PASS: " << mode << ": " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}

#include "model.hpp"
#include "solver.hpp"
#include "simulator.hpp"
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

using namespace queue_control;
namespace {
std::uint64_t integer(const std::string& text, const std::string& option, std::uint64_t maximum) {
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value > maximum)
        throw std::invalid_argument("invalid or out-of-range value for " + option);
    return value;
}
double tolerance(const std::string& text, const std::string& option) {
    std::size_t consumed = 0;
    double value = 0;
    try { value = std::stod(text, &consumed); }
    catch (const std::exception&) { throw std::invalid_argument("invalid value for " + option); }
    if (consumed != text.size() || text.find_first_of(" \t\r\n") != std::string::npos ||
        !std::isfinite(value) || value <= 0)
        throw std::invalid_argument("expected finite positive tolerance for " + option);
    return value;
}
void diagnostics(const char* name, const Diagnostics& value) {
    std::cout << name << ": converged=" << (value.converged ? "yes" : "no")
              << ", iterations=" << value.iterations << ", max residual=" << value.max_residual << '\n';
}
void export_csv(const std::string& path, const Solution& solution) {
    std::ofstream output(path);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << "bid_qty,ask_qty,p_ask_first,expected_seconds\n" << std::setprecision(17);
    for (int bid = 1; bid <= solution.cap; ++bid)
        for (int ask = 1; ask <= solution.cap; ++ask)
            output << bid << ',' << ask << ',' << solution.p(bid, ask) << ','
                   << solution.seconds(bid, ask) << '\n';
    output.close();
}
}
int main(int argc, char** argv) {
    try {
        std::cout.exceptions(std::ios::failbit | std::ios::badbit);
        int cap = 20, bid = 5, ask = 5;
        std::string model_name = "constant", csv;
        SolverOptions solver_options;
        SimulationOptions simulation_options;
        std::set<std::string> seen;
        bool help = false;
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (!seen.insert(option).second) throw std::invalid_argument("duplicate option: " + option);
            if (option == "--help") { help = true; continue; }
            const std::set<std::string> valued = {"--model", "--qmax", "--bid", "--ask", "--runs",
                "--seed", "--event-limit", "--prob-tol", "--time-tol", "--max-iterations", "--csv"};
            if (!valued.count(option)) throw std::invalid_argument("unknown option: " + option);
            if (i + 1 == argc || std::string(argv[i + 1]).rfind("--", 0) == 0)
                throw std::invalid_argument("missing value for " + option);
            const std::string value = argv[++i];
            if (option == "--model") model_name = value;
            else if (option == "--qmax") cap = static_cast<int>(integer(value, option, max_queue_cap));
            else if (option == "--bid") bid = static_cast<int>(integer(value, option, max_queue_cap));
            else if (option == "--ask") ask = static_cast<int>(integer(value, option, max_queue_cap));
            else if (option == "--runs") simulation_options.runs = integer(value, option, 10000000);
            else if (option == "--seed") simulation_options.seed = integer(value, option, UINT64_MAX);
            else if (option == "--event-limit") simulation_options.event_limit = integer(value, option, 100000000);
            else if (option == "--max-iterations") solver_options.max_iterations = integer(value, option, 1000000);
            else if (option == "--prob-tol") solver_options.probability_tolerance = tolerance(value, option);
            else if (option == "--time-tol") solver_options.duration_tolerance = tolerance(value, option);
            else if (option == "--csv") {
                if (value.empty()) throw std::invalid_argument("CSV path cannot be empty");
                csv = value;
            }
        }
        if (model_name != "constant" && model_name != "linear")
            throw std::invalid_argument("model must be constant or linear");
        validate_start(bid, ask, cap);
        if (simulation_options.runs < 2 || simulation_options.event_limit == 0 || solver_options.max_iterations == 0)
            throw std::invalid_argument("runs must be at least 2; event limit and max iterations must be positive");
        if (help) {
            std::cout << "queue_control [options]\n"
                "  --model constant|linear    (constant)\n"
                "  --qmax 1..100              (20)\n"
                "  --bid N --ask N            initial quantities, 1..qmax (5,5)\n"
                "  --runs 2..10000000         (100000)\n"
                "  --seed UINT64              (42)\n"
                "  --event-limit 1..100000000 per path (1000000)\n"
                "  --prob-tol POSITIVE        probability residual tolerance (1e-11)\n"
                "  --time-tol POSITIVE        duration residual tolerance in seconds (1e-10)\n"
                "  --max-iterations 1..1000000 per grid (100000)\n"
                "  --csv PATH                export interior solver grid\n"
                "  --help\n";
            std::cout.flush();
            return 0;
        }
        const Model model = model_name == "constant" ? constant_model() : linear_model();
        std::cout << std::setprecision(12)
                  << "Model: " << model_name << " (synthetic, uncalibrated); rates in events/second\n"
                  << "lambda_b=lambda_a=1; mu_b=1+" << model.bid_removal_slope
                  << "*b; mu_a=1+" << model.ask_removal_slope << "*a\n"
                  << "Queue cap=" << cap << "; initial bid=" << bid << "; initial ask=" << ask
                  << "; seed=" << simulation_options.seed << '\n';
        const auto solution = solve(model, cap, solver_options);
        diagnostics("Probability", solution.probability_diagnostics);
        diagnostics("Duration (seconds)", solution.duration_diagnostics);
        if (!solution.converged()) throw std::runtime_error("solver did not converge within iteration limit");
        std::cout << "Solver: P(ask depletes first)=" << solution.p(bid, ask)
                  << "; expected seconds=" << solution.seconds(bid, ask) << '\n';
        if (!csv.empty()) export_csv(csv, solution);
        const auto result = simulate(model, cap, bid, ask, simulation_options);
        std::cout << "Monte Carlo paths: requested=" << result.requested << "; completed=" << result.completed << '\n';
        if (!result.estimates) throw std::runtime_error("incomplete experiment: per-path event limit reached; no Monte Carlo estimates published");
        const auto& estimate = *result.estimates;
        std::cout << "Monte Carlo: P(ask depletes first)=" << estimate.ask_first_frequency
                  << "; 95% Wilson interval=[" << estimate.probability_interval.lower << ", "
                  << estimate.probability_interval.upper << "]\n"
                  << "Duration: mean seconds=" << estimate.mean_seconds << "; sample SD=" << estimate.duration_stddev
                  << "; standard error=" << estimate.duration_stderr << "; approximate 95% mean interval=["
                  << estimate.duration_interval.lower << ", " << estimate.duration_interval.upper << "]\n"
                  << "Monte Carlo minus solver: probability=" << estimate.ask_first_frequency - solution.p(bid, ask)
                  << "; seconds=" << estimate.mean_seconds - solution.seconds(bid, ask) << '\n';
        std::cout.flush();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}

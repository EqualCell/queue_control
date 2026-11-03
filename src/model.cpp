#include "model.hpp"
#include <cmath>
#include <initializer_list>
#include <stdexcept>

namespace queue_control {
double Rates::total() const {
    return bid_addition + bid_removal + ask_addition + ask_removal;
}
Rates Model::rates(int bid, int ask, int cap) const {
    return {bid == cap ? 0.0 : bid_addition,
            bid_removal_intercept + bid_removal_slope * bid,
            ask == cap ? 0.0 : ask_addition,
            ask_removal_intercept + ask_removal_slope * ask};
}
Model constant_model() { return {}; }
Model linear_model() {
    Model model;
    model.bid_removal_slope = model.ask_removal_slope = 0.1;
    return model;
}
void validate_cap(int cap) {
    if (cap < 1 || cap > max_queue_cap)
        throw std::invalid_argument("qmax must be in [1, 100]");
}
void validate_start(int bid, int ask, int cap) {
    validate_cap(cap);
    if (bid < 1 || ask < 1 || bid > cap || ask > cap)
        throw std::invalid_argument("initial bid and ask must be in [1, qmax]");
}
void validate_model(const Model& model, int cap) {
    validate_cap(cap);
    for (double value : {model.bid_addition, model.ask_addition})
        if (!std::isfinite(value) || value < 0)
            throw std::invalid_argument("addition rates must be finite and nonnegative");
    for (double value : {model.bid_removal_intercept, model.ask_removal_intercept,
                         model.bid_removal_slope, model.ask_removal_slope})
        if (!std::isfinite(value))
            throw std::invalid_argument("removal parameters must be finite");
    for (int bid = 1; bid <= cap; ++bid) {
        for (int ask = 1; ask <= cap; ++ask) {
            const auto rate = model.rates(bid, ask, cap);
            if (!std::isfinite(rate.bid_removal) || rate.bid_removal <= 0 ||
                !std::isfinite(rate.ask_removal) || rate.ask_removal <= 0)
                throw std::invalid_argument("removal rates must be finite and strictly positive");
            if (!std::isfinite(rate.total()) || rate.total() <= 0)
                throw std::invalid_argument("effective total rate must be finite and positive");
        }
    }
}
} // namespace queue_control

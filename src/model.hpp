#pragma once

namespace queue_control {
constexpr int max_queue_cap = 100;

struct Rates {
    double bid_addition, bid_removal, ask_addition, ask_removal;
    double total() const;
};

// Removal rates are intercept + slope * own queue quantity.
struct Model {
    double bid_addition = 1.0;
    double ask_addition = 1.0;
    double bid_removal_intercept = 1.0;
    double ask_removal_intercept = 1.0;
    double bid_removal_slope = 0.0;
    double ask_removal_slope = 0.0;
    Rates rates(int bid, int ask, int cap) const;
};

Model constant_model();
Model linear_model();
void validate_cap(int cap);
void validate_start(int bid, int ask, int cap);
void validate_model(const Model& model, int cap);
} // namespace queue_control

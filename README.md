# queue-control — Milestone 1

Given initial bid and ask queue quantities, what is the probability that the
**ask depletes first**, and how many seconds are expected until either queue
empties? This C++17 program answers that question for a finite continuous-time
Markov chain (CTMC), and independently checks the answer with Monte Carlo.
It stops at first depletion. There is no trading policy or profitability claim.

## Build and run

Requires a C++17 compiler and Make. Python 3 (standard library only) is needed
only for CLI integration tests. There are no third-party numerical dependencies.

```sh
make
make test
./queue_control --model constant --qmax 20 --bid 5 --ask 5 --runs 100000 --seed 42
./queue_control --model linear --qmax 20 --bid 5 --ask 5 --runs 100000 --seed 42
./queue_control --csv build/grid.csv
./queue_control --help
```

Default compiler flags: `-std=c++17 -O2 -Wall -Wextra -Wpedantic`.
`make clean` removes generated binaries and build outputs. Override `CXX` or
`CXXFLAGS` if needed. For example, to verify tests without assertions enabled:

```sh
make clean
make test CXXFLAGS='-std=c++17 -O2 -Wall -Wextra -Wpedantic -DNDEBUG'
```

Defaults match the constant example above. Optional controls are `--prob-tol`
(default 1e-11), `--time-tol` (1e-10 seconds), `--max-iterations` (100000 per
quantity), and `--event-limit` (1000000 per path). Every option may occur only
once. Parsing and computation failures exit nonzero. CSV exports interior
states only, with columns
`bid_qty,ask_qty,p_ask_first,expected_seconds`; an existing destination is
overwritten, and open/write/close failures are checked.

Supported limits are K in [1,100], starting quantities in [1,K], runs in
[2,10000000], iterations in [1,1000000], and events per path in [1,100000000].
At least two paths are required for sample duration standard deviation.
The seed is an unsigned 64-bit integer. Sizes are validated before grid
allocation or simulation. K=100 is a modest memory cap, not a runtime guarantee:
strong upward drift or demanding tolerances may exhaust an iteration/event
budget. Limits are individual bounds, not a bound on total computational work.

## Model and equations

At interior state (b,a), 1 <= b,a <= K, each event changes one quantity by one:

| Event | Destination | Rate (events/second) |
|---|---|---|
| Bid addition | (b+1,a) | lambda_b |
| Bid removal | (b-1,a) | mu_b |
| Ask addition | (b,a+1) | lambda_a |
| Ask removal | (b,a-1) | mu_a |

At b=K bid additions are disabled; at a=K ask additions are disabled. They do
not count toward the exit rate R and do not have simulation clocks. They are
not self-transitions. Stop immediately when a queue reaches zero. The corner
(0,0) is unreachable: it has no boundary value, its storage contains NaN, and
access through the solution API rejects it.

The constant preset sets all four nominal rates to 1. The linear preset sets
both additions to 1 and removals to mu_b=1+0.1*b and mu_a=1+0.1*a. The latter
is an uncalibrated toy model, not a validated cancellation law. The public
`Model` struct allows independent additions and affine removal rates for each
side, including asymmetry, no additions, and uniformly scaled rates. Validation
checks every interior state's removal rates and effective total. Additions must
be finite and nonnegative, removals finite and strictly positive, and R finite
and positive. These conditions on this finite chain imply eventual absorption
with finite mathematical expected time; extremely large/small rates can still
exceed floating-point representability and produce a reported numerical error.

An enabled event with rate r has an exponential clock with mean 1/r seconds.
The minimum of these clocks has mean 1/R, and each event wins with probability
r/R. Conditioning on this first event gives, with sums over enabled neighbors:

```text
u(b,a) = sum[r * u(neighbor)] / R
w(b,a) = 1/R + sum[r * w(neighbor)] / R
u(b,0) = 1, u(0,a) = 0       (positive other coordinate)
w(b,0) = 0, w(0,a) = 0       (positive other coordinate)
```

For example, at (1,1) with no additions and removal rates 1 (bid) and 3 (ask),
R=4. The probability update is `(1*0 + 3*1)/4 = 0.75`, and the duration update
is `(1 + 1*0 + 3*0)/4 = 0.25` seconds. With additions, returning to previously
visited states creates cycles: probabilities and durations must satisfy
simultaneous equations rather than a single forward recursion.

## Numerical solver

`src/solver.cpp` stores two (K+1)-by-(K+1) grids and effective rates, using
O(K^2) memory, never a dense K^2-by-K^2 matrix. Interior values start at zero.
Gauss–Seidel sweeps visit bid then ask in increasing order, immediately using
new values where available. Probability and duration grids converge separately.
The implementation multiplies neighboring values by r/R to avoid avoidable
intermediate overflow; it does not clip probabilities.

After each sweep, the solver recomputes the maximum absolute first-step equation
residual over the *complete current grid*. Probability residuals are
unitless; duration residuals are seconds. Each grid stops when its own residual
is at most its tolerance. The CLI reports both iteration counts and residuals.
An iteration limit reports nonconvergence and prevents a successful CLI result
or CSV export. Nonfinite numerical values are errors. A small residual is
**not a guaranteed bound on true solution error**, especially for slowly
absorbing chains.

## Independent simulation and uncertainty

`src/simulator.cpp` shares model evaluation and validation only. It uses neither
solver grids nor solver neighbor assembly. At every state it draws one
independent exponential waiting time per positive enabled event, takes the
minimum, adds that time, and updates queues via an explicit four-case switch.
It redraws the clocks after each event, valid by exponential memorylessness.
`std::mt19937_64` uses the explicit seed; repeatability is for the same
executable/configuration. Standard-library distribution implementations need
not produce identical samples across platforms.

The output includes requested/completed paths, ask-first frequency with a 95%
Wilson interval, and mean duration with sample standard deviation, standard
error SD/sqrt(n), and an approximate normal 95% interval mean +/- 1.959964*SE.
Welford's recurrence accumulates mean and variance. The duration interval is
an asymptotic approximation, potentially poor for small samples or heavy
right tails, and is not clipped at zero. Neither interval includes model or
numerical error.

A path that needs more than its event budget ends the experiment immediately.
Completion counts are reported, the CLI exits nonzero, and *no* Monte Carlo
estimates are exposed for that partial experiment. Absorption exactly at the
budget is allowed. A solver CSV already exported remains a valid solver-only
artifact even if the subsequent simulation fails.

## Files and verification

- `src/model.hpp`, `src/model.cpp`: rates, presets, and input validation.
- `src/solver.hpp`, `src/solver.cpp`: grids, iteration, and convergence diagnostics.
- `src/simulator.hpp`, `src/simulator.cpp`: independent clocks and statistics.
- `src/main.cpp`: parsing, reporting, error handling, and CSV export.
- `tests/tests.cpp`: deterministic mathematics and separate statistical checks.
- `tests/cli_tests.py`: argument rejection, failure reporting, and CSV/output checks.
- `Makefile`, `.gitignore`: build targets and generated-output exclusions.

Tests use explicit failures, not `assert`, and work with `-DNDEBUG`.
Deterministic tests cover analytic races and pure depletion, both presets'
symmetry, rate scaling, absorbing boundaries, cap behavior, recomputed residuals,
nonconvergence, validation, fixed-seed repeatability, and event-limit failure.
Six preselected 50000-path statistical cases compare with analytic or solver
values using generous six-standard-error tolerances, without seed retries.
Monte Carlo agreement supports correctness; it is not a mathematical proof.

Observed example results (K=20, initial (5,5), 100000 paths, seed 42):

| Preset | Solver ask-first | MC ask-first (95% Wilson) | Solver seconds | MC seconds (approx. 95% mean interval) |
|---|---|---|---|---|
| constant | 0.499999999482 | 0.49842 [0.495321, 0.501519] | 29.490609 | 29.328055 [29.008126, 29.647985] |
| linear | 0.499999999952 | 0.50151 [0.498411, 0.504609] | 6.716049 | 6.718781 [6.688083, 6.749479] |

Constant: probability/duration iterations 3118/3582, residuals
9.981e-12 / 9.948e-11 seconds; duration SD 51.618601, SE 0.163232.
Linear: iterations 291/294, residuals 9.827e-12 / 9.403e-11 seconds;
duration SD 4.952922, SE 0.015663. All requested paths completed.

## Assumptions and scope

Rates are synthetic and uncalibrated. Events are one unit; quantities are
capped; rates depend only on current quantities; the process ends at first
depletion. There is no own order, queue position, price reset, historical
calibration, networking, strategy, account management, or profitability claim.
The finite cap changes the process. No claim of market accuracy, research
novelty, or replication of a full order-book model is made.

Background only:

- [Huang, Lehalle, Rosenbaum: Simulating and analyzing order book data: The queue-reactive model](https://arxiv.org/abs/1312.0563).
- [Rick Durrett: Continuous Time Markov Chains](https://sites.math.duke.edu/~rtd/survey/surva2.html).

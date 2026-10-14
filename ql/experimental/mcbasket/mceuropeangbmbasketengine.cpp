/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2026 Michael Li

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it
 under the terms of the QuantLib license.  You should have received a
 copy of the license along with this program; if not, please email
 <quantlib-dev@lists.sf.net>. The license is also available online at
 <https://www.quantlib.org/license.shtml>.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

#include <ql/exercise.hpp>
#include <ql/experimental/mcbasket/mceuropeangbmbasketengine.hpp>
#include <ql/math/distributions/normaldistribution.hpp>
#include <ql/math/matrixutilities/pseudosqrt.hpp>
#include <ql/math/randomnumbers/mt19937uniformrng.hpp>
#include <ql/math/statistics/generalstatistics.hpp>
#include <ql/termstructures/volatility/equityfx/blackconstantvol.hpp>
#include <ql/timegrid.hpp>
#include <algorithm>
#include <exception>
#include <numeric>
#include <utility>

namespace QuantLib {

    namespace {

        // Everything about the paths that doesn't depend on the random draws.
        struct Paths {
            const Matrix& sqrtCorrelation;
            const Matrix& stdDev; // per time step (row) and asset (column)
            const Matrix& drift;  // of the log-price, idem
            const Array& x0;
            const BasketPayoff& payoff;
            DiscountFactor discount;
            bool antithetic;
        };

        /* Writes the discounted payoff of each path into [begin, end), taking
           the Gaussian variates from rng in the order of MCEuropeanBasketEngine:
           for each path, one per asset at the first step, then at the second,
           and so on. */
        void simulate(const Paths& in, MersenneTwisterUniformRng& rng, Real* begin, Real* end) {
            const Size n = in.x0.size(), steps = in.drift.rows(), dimension = n * steps;
            const Size lanes = in.antithetic ? 2 * n : n;
            // Paths are processed in batches of about 4096 variates (32 kB), so
            // that drawing the variates, evolving the paths and taking the
            // exponentials run as separate tight loops; this is faster than
            // doing all three path by path.
            const Size batchSize = std::max<Size>(4096 / dimension, 1);
            std::vector<Real> w(batchSize * dimension), growth(batchSize * lanes);
            Array terminal(n);
            while (begin != end) {
                const Size paths = std::min<Size>(batchSize, end - begin);
                for (Size k = 0; k < paths * dimension; ++k)
                    w[k] = InverseCumulativeNormal::standard_value(rng.nextReal());
                // The log-growth of each asset is the sum over the steps of
                // stdDev * dz + drift, where dz are the correlated increments
                // of StochasticProcessArray::evolve and each term is written as
                // in GeneralizedBlackScholesProcess::evolve, so that a single
                // step gives identical results.  The antithetic path has
                // increments -dz.
                for (Size p = 0; p < paths; ++p) {
                    const Real* wp = &w[p * dimension];
                    Real* gp = &growth[p * lanes];
                    for (Size j = 0; j < n; ++j) {
                        Real x = 0.0, y = 0.0;
                        for (Size i = 0; i < steps; ++i) {
                            const Real* wi = wp + i * n;
                            const Real dz = std::inner_product(
                                wi, wi + n, in.sqrtCorrelation.row_begin(j), Real(0.0));
                            x += in.stdDev[i][j] * dz + in.drift[i][j];
                            y += in.stdDev[i][j] * -dz + in.drift[i][j];
                        }
                        gp[j] = x;
                        if (in.antithetic)
                            gp[n + j] = y;
                    }
                }
                for (Size k = 0; k < paths * lanes; ++k)
                    growth[k] = std::exp(growth[k]);
                const auto value = [&](const Real* g) {
                    for (Size j = 0; j < n; ++j)
                        terminal[j] = in.x0[j] * g[j];
                    return in.payoff(terminal) * in.discount;
                };
                for (Size p = 0; p < paths; ++p) {
                    const Real* gp = &growth[p * lanes];
                    Real price = value(gp);
                    if (in.antithetic)
                        price = (price + value(gp + n)) / 2.0;
                    begin[p] = price;
                }
                begin += paths;
            }
        }

    }

    MCEuropeanGBMBasketEngine::MCEuropeanGBMBasketEngine(
        std::vector<ext::shared_ptr<GeneralizedBlackScholesProcess>> processes,
        const Matrix& rho,
        Size timeSteps,
        Size requiredSamples,
        bool antitheticVariate,
        BigNatural seed,
        Size threads)
    : processes_(std::move(processes)), timeSteps_(timeSteps), requiredSamples_(requiredSamples),
      antitheticVariate_(antitheticVariate), seed_(seed), threads_(threads) {
        QL_REQUIRE(!processes_.empty(), "no processes given");
        QL_REQUIRE(rho.rows() == processes_.size() && rho.columns() == processes_.size(),
                   "correlation matrix and number of processes don't match");
        QL_REQUIRE(timeSteps_ > 0, "timeSteps must be positive");
        QL_REQUIRE(requiredSamples_ > 1, "at least two samples are required");
        QL_REQUIRE(threads_ > 0, "at least one thread is required");
        // the same root as StochasticProcessArray, hence the same paths
        sqrtCorrelation_ = pseudoSqrt(rho, SalvagingAlgorithm::Spectral);
        for (const auto& process : processes_)
            registerWith(process);
    }

    void MCEuropeanGBMBasketEngine::calculate() const {
        const auto payoff = ext::dynamic_pointer_cast<BasketPayoff>(arguments_.payoff);
        QL_REQUIRE(payoff, "non-basket payoff given");
        QL_REQUIRE(arguments_.exercise->type() == Exercise::European, "not an European option");
        const Date maturity = arguments_.exercise->lastDate();

        const Size n = processes_.size();
        const TimeGrid grid(processes_[0]->time(maturity), timeSteps_);
        Matrix stdDev(timeSteps_, n), drift(timeSteps_, n);
        Array x0(n);
        for (Size j = 0; j < n; ++j) {
            const auto& process = processes_[j];
            // with constant volatility, the process evolves exactly, with
            // a drift and variance over each step that don't depend on the path
            QL_REQUIRE(ext::dynamic_pointer_cast<BlackConstantVol>(*process->blackVolatility()),
                       "constant Black volatility required");
            x0[j] = process->x0();
            for (Size i = 0; i < timeSteps_; ++i) {
                // as in GeneralizedBlackScholesProcess::evolve
                const Time t = grid[i], dt = grid.dt(i);
                const Real variance = process->variance(t, x0[j], dt);
                const Rate r =
                    process->riskFreeRate()->forwardRate(t, t + dt, Continuous, NoFrequency, true);
                const Rate q =
                    process->dividendYield()->forwardRate(t, t + dt, Continuous, NoFrequency, true);
                stdDev[i][j] = std::sqrt(variance);
                drift[i][j] = (r - q) * dt - 0.5 * variance;
            }
        }
        const DiscountFactor discount = processes_[0]->riskFreeRate()->discount(maturity);

        const Paths paths{sqrtCorrelation_, stdDev, drift, x0,
                          *payoff, discount, antitheticVariate_};
        std::vector<Real> samples(requiredSamples_);
        MersenneTwisterUniformRng rng(seed_);
        if (threads_ == 1) {
            simulate(paths, rng, samples.data(), samples.data() + samples.size());
        } else {
            // Each chunk of paths draws its variates from its own position in
            // the sequence.  The positions are found first, by skipping over
            // the variates of the previous chunks; this takes a fraction of
            // the time needed to draw them.
            const Size chunks = 4 * threads_;
            const auto first = [&](Size k) { return samples.data() + k * samples.size() / chunks; };
            std::vector<MersenneTwisterUniformRng> generators;
            generators.reserve(chunks);
            for (Size k = 0; k < chunks; ++k) {
                generators.push_back(rng);
                rng.discard((first(k + 1) - first(k)) * n * timeSteps_);
            }
            std::exception_ptr error;
            #pragma omp parallel for schedule(dynamic) num_threads(static_cast<int>(threads_))
            for (long c = 0; c < static_cast<long>(chunks); ++c) {
                const auto k = static_cast<Size>(c);
                // exceptions must not leave a parallel region
                try {
                    simulate(paths, generators[k], first(k), first(k + 1));
                } catch (...) {
                    #pragma omp critical
                    error = std::current_exception();
                }
            }
            if (error)
                std::rethrow_exception(error);
        }

        GeneralStatistics statistics;
        statistics.reserve(samples.size());
        statistics.addSequence(samples.begin(), samples.end());
        results_.value = statistics.mean();
        results_.errorEstimate = statistics.errorEstimate();
    }

}

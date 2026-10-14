/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2005 Klaus Spanderen
 Copyright (C) 2005 StatPro Italia srl

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

#include <ql/processes/stochasticprocessarray.hpp>
#include <ql/math/matrixutilities/pseudosqrt.hpp>
#include <numeric>
#include <typeinfo>
#include <utility>

namespace QuantLib {

    namespace {

        // evolves each x0[i] by evolve(i, x0[i], dz[i]) with dz = sqrtCorrelation * dw
        template <class F>
        Array evolveCorrelated(const Matrix& sqrtCorrelation,
                               const Array& x0,
                               const Array& dw,
                               const F& evolve) {
            QL_REQUIRE(dw.size() == sqrtCorrelation.columns(),
                       "wrong number of variates (" << dw.size() << ", "
                                                    << sqrtCorrelation.columns() << " required)");

            Array tmp(sqrtCorrelation.rows());
            for (Size i = 0; i < tmp.size(); ++i) {
                // the i-th element of sqrtCorrelation * dw, without allocating the product
                Real dz = std::inner_product(dw.begin(), dw.end(), sqrtCorrelation.row_begin(i),
                                             Real(0.0));
                tmp[i] = evolve(i, x0[i], dz);
            }
            return tmp;
        }

        class CorrelatedStep : public StochasticProcess::Step {
          public:
            CorrelatedStep(std::vector<ext::shared_ptr<StochasticProcess::Step>> steps,
                           const Matrix& sqrtCorrelation)
            : steps_(std::move(steps)), sqrtCorrelation_(sqrtCorrelation) {}
            Array evolve(const Array& x0, const Array& dw) const override {
                return evolveCorrelated(sqrtCorrelation_, x0, dw, [&](Size i, Real x, Real dz) {
                    return steps_[i]->evolve(x, dz);
                });
            }

          private:
            std::vector<ext::shared_ptr<StochasticProcess::Step>> steps_;
            const Matrix& sqrtCorrelation_;
        };

    }

    StochasticProcessArray::StochasticProcessArray(
        const std::vector<ext::shared_ptr<StochasticProcess1D> >& processes,
        const Matrix& correlation)
    : processes_(processes),
      sqrtCorrelation_(pseudoSqrt(correlation,SalvagingAlgorithm::Spectral)) {

        QL_REQUIRE(!processes.empty(), "no processes given");
        QL_REQUIRE(correlation.rows() == processes.size(),
                   "mismatch between number of processes "
                   "and size of correlation matrix");
        for (auto& process : processes_) {
            QL_REQUIRE(process, "null 1-D stochastic process");
            registerWith(process);
        }
    }

    Size StochasticProcessArray::size() const {
        return processes_.size();
    }

    Array StochasticProcessArray::initialValues() const {
        Array tmp(size());
        for (Size i=0; i<size(); ++i)
            tmp[i] = processes_[i]->x0();
        return tmp;
    }

    Array StochasticProcessArray::drift(Time t,
                                        const Array& x) const {
        Array tmp(size());
        for (Size i=0; i<size(); ++i)
            tmp[i] = processes_[i]->drift(t, x[i]);
        return tmp;
    }

    Matrix StochasticProcessArray::diffusion(Time t,
                                             const Array& x) const {
        Matrix tmp = sqrtCorrelation_;
        for (Size i=0; i<size(); ++i) {
            Real sigma = processes_[i]->diffusion(t, x[i]);
            std::transform(tmp.row_begin(i), tmp.row_end(i),
                           tmp.row_begin(i),
                           [=](Real x) -> Real { return x * sigma; });
        }
        return tmp;
    }

    Array StochasticProcessArray::expectation(Time t0,
                                              const Array& x0,
                                              Time dt) const {
        Array tmp(size());
        for (Size i=0; i<size(); ++i)
            tmp[i] = processes_[i]->expectation(t0, x0[i], dt);
        return tmp;
    }

    Matrix StochasticProcessArray::stdDeviation(Time t0,
                                                const Array& x0,
                                                Time dt) const {
        Matrix tmp = sqrtCorrelation_;
        for (Size i=0; i<size(); ++i) {
            Real sigma = processes_[i]->stdDeviation(t0, x0[i], dt);
            std::transform(tmp.row_begin(i), tmp.row_end(i),
                           tmp.row_begin(i),
                           [=](Real x) -> Real { return x * sigma; });
        }
        return tmp;
    }

    Matrix StochasticProcessArray::covariance(Time t0,
                                              const Array& x0,
                                              Time dt) const {
        Matrix tmp = stdDeviation(t0, x0, dt);
        return tmp*transpose(tmp);
    }

    Array StochasticProcessArray::evolve(
                  Time t0, const Array& x0, Time dt, const Array& dw) const {
        return evolveCorrelated(sqrtCorrelation_, x0, dw, [&](Size i, Real x, Real dz) {
            return processes_[i]->evolve(t0, x, dt, dz);
        });
    }

    ext::shared_ptr<StochasticProcess::Step> StochasticProcessArray::step(Time t0, Time dt) const {
        // a derived class might override evolve()
        if (typeid(*this) != typeid(StochasticProcessArray))
            return StochasticProcess::step(t0, dt);

        std::vector<ext::shared_ptr<Step>> steps;
        steps.reserve(size());
        for (const auto& process : processes_)
            steps.push_back(process->step(t0, dt));
        return ext::make_shared<CorrelatedStep>(std::move(steps), sqrtCorrelation_);
    }

    Array StochasticProcessArray::apply(const Array& x0,
                                        const Array& dx) const {
        Array tmp(size());
        for (Size i=0; i<size(); ++i)
            tmp[i] = processes_[i]->apply(x0[i],dx[i]);
        return tmp;
    }

    Time StochasticProcessArray::time(const Date& d) const {
        return processes_[0]->time(d);
    }

    const ext::shared_ptr<StochasticProcess1D>&
    StochasticProcessArray::process(Size i) const {
        return processes_[i];
    }

    Matrix StochasticProcessArray::correlation() const {
        return sqrtCorrelation_ * transpose(sqrtCorrelation_);
    }

}

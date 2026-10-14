/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2003 Ferdinando Ametrano
 Copyright (C) 2001, 2002, 2003 Sadruddin Rejeb
 Copyright (C) 2004, 2005 StatPro Italia srl

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

#include <ql/stochasticprocess.hpp>
#include <utility>

namespace QuantLib {

    namespace {

        // the default steps forward to the evolve() methods of the process

        class EvolveStep : public StochasticProcess::Step {
          public:
            EvolveStep(const StochasticProcess& process, Time t0, Time dt)
            : process_(process), t0_(t0), dt_(dt) {}
            Array evolve(const Array& x0, const Array& dw) const override {
                return process_.evolve(t0_, x0, dt_, dw);
            }

          private:
            const StochasticProcess& process_;
            Time t0_, dt_;
        };

        class EvolveStep1D : public StochasticProcess1D::Step {
          public:
            EvolveStep1D(const StochasticProcess1D& process, Time t0, Time dt)
            : process_(process), t0_(t0), dt_(dt) {}
            Real evolve(Real x0, Real dw) const override {
                return process_.evolve(t0_, x0, dt_, dw);
            }

          private:
            const StochasticProcess1D& process_;
            Time t0_, dt_;
        };

    }


    // steps

    Real StochasticProcess::Step::evolve(Real x0, Real dw) const {
        return evolve(Array(1, x0), Array(1, dw))[0];
    }

    Array StochasticProcess1D::Step::evolve(const Array& x0, const Array& dw) const {
        return Array(1, evolve(x0[0], dw[0]));
    }


    // base class

    StochasticProcess::StochasticProcess(ext::shared_ptr<discretization> disc)
    : discretization_(std::move(disc)) {}

    Size StochasticProcess::factors() const {
        return size();
    }

    Array StochasticProcess::expectation(Time t0,
                                         const Array& x0,
                                         Time dt) const {
        return apply(x0, discretization_->drift(*this, t0, x0, dt));
    }

    Matrix StochasticProcess::stdDeviation(Time t0,
                                           const Array& x0,
                                           Time dt) const {
        return discretization_->diffusion(*this, t0, x0, dt);
    }

    Matrix StochasticProcess::covariance(Time t0,
                                         const Array& x0,
                                         Time dt) const {
        return discretization_->covariance(*this, t0, x0, dt);
    }

    Array StochasticProcess::evolve(Time t0, const Array& x0,
                                    Time dt, const Array& dw) const {
        return apply(expectation(t0,x0,dt), stdDeviation(t0,x0,dt)*dw);
    }

    ext::shared_ptr<StochasticProcess::Step> StochasticProcess::step(Time t0, Time dt) const {
        return ext::make_shared<EvolveStep>(*this, t0, dt);
    }

    Array StochasticProcess::apply(const Array& x0,
                                   const Array& dx) const {
        return x0 + dx;
    }

    Time StochasticProcess::time(const Date& ) const {
        QL_FAIL("date/time conversion not supported");
    }

    void StochasticProcess::update() {
        notifyObservers();
    }


    // 1-D specialization

    StochasticProcess1D::StochasticProcess1D(ext::shared_ptr<discretization> disc)
    : discretization_(std::move(disc)) {}

    Real StochasticProcess1D::expectation(Time t0, Real x0, Time dt) const {
        return apply(x0, discretization_->drift(*this, t0, x0, dt));
    }

    Real StochasticProcess1D::stdDeviation(Time t0, Real x0, Time dt) const {
        return discretization_->diffusion(*this, t0, x0, dt);
    }

    Real StochasticProcess1D::variance(Time t0, Real x0, Time dt) const {
        return discretization_->variance(*this, t0, x0, dt);
    }

    Real StochasticProcess1D::evolve(Time t0, Real x0,
                                     Time dt, Real dw) const {
        return apply(expectation(t0,x0,dt), stdDeviation(t0,x0,dt)*dw);
    }

    ext::shared_ptr<StochasticProcess::Step> StochasticProcess1D::step(Time t0, Time dt) const {
        return ext::make_shared<EvolveStep1D>(*this, t0, dt);
    }

    Real StochasticProcess1D::apply(Real x0, Real dx) const {
        return x0 + dx;
    }

}

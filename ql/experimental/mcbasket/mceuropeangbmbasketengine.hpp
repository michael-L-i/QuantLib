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

/*! \file mceuropeangbmbasketengine.hpp
    \brief Monte Carlo engine for European basket options on correlated GBMs
*/

#ifndef quantlib_mc_european_gbm_basket_engine_hpp
#define quantlib_mc_european_gbm_basket_engine_hpp

#include <ql/instruments/basketoption.hpp>
#include <ql/processes/blackscholesprocess.hpp>

namespace QuantLib {

    //! Monte Carlo engine for European basket options on correlated GBMs
    /*! This is a specialization of MCEuropeanBasketEngine<PseudoRandom> for
        Black-Scholes processes with constant volatility.  Their drift and
        variance over a time step don't depend on the path, so they are
        computed once per step instead of once per step, path and asset.
        Paths are evolved in log space without being stored, with a single
        exponential per asset at maturity, and antithetic paths reuse the
        correlated increments of the original ones.

        Everything else is as in MCEuropeanBasketEngine: the random sequence
        (Mersenne twister and inverse cumulative normal), the correlation
        root (spectral pseudo-square root), the time grid and the estimator.
        Given the same processes, correlation, steps, samples and seed, the
        two engines return the same value and error estimate; with more than
        one time step they agree up to rounding, since the exponential of a
        sum replaces a product of exponentials.

        As the terminal values are exactly log-normal, a single time step
        suffices; more steps only reproduce the results of
        MCEuropeanBasketEngine with the same number of steps.

        \ingroup basketengines

        \test the results are checked against MCEuropeanBasketEngine.
    */
    class MCEuropeanGBMBasketEngine : public BasketOption::engine {
      public:
        MCEuropeanGBMBasketEngine(
            std::vector<ext::shared_ptr<GeneralizedBlackScholesProcess>> processes,
            const Matrix& rho,
            Size timeSteps,
            Size requiredSamples,
            bool antitheticVariate = false,
            BigNatural seed = 0);
        void calculate() const override;

      private:
        std::vector<ext::shared_ptr<GeneralizedBlackScholesProcess>> processes_;
        Matrix sqrtCorrelation_;
        Size timeSteps_, requiredSamples_;
        bool antitheticVariate_;
        BigNatural seed_;
    };

}

#endif

/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2008 Andreas Gaida
 Copyright (C) 2008, 2009 Ralph Schreyer
 Copyright (C) 2008, 2009 Klaus Spanderen

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

#include <ql/instruments/payoffs.hpp>
#include <ql/math/functional.hpp>
#include <ql/methods/finitedifferences/meshers/fdmmesher.hpp>
#include <ql/methods/finitedifferences/operators/fdmblackscholesop.hpp>
#include <ql/methods/finitedifferences/operators/fdmlinearoplayout.hpp>
#include <ql/methods/finitedifferences/operators/secondderivativeop.hpp>
#include <ql/termstructures/volatility/equityfx/blackconstantvol.hpp>
#include <ql/termstructures/yield/flatforward.hpp>
#include <utility>

namespace QuantLib {

    namespace {
        // a flat forward curve has the same continuous forward rate everywhere
        // unless it compounds simply over some period
        bool isFlat(const ext::shared_ptr<YieldTermStructure>& ts) {
            const auto flat = ext::dynamic_pointer_cast<FlatForward>(ts);
            return flat != nullptr
                && (flat->compounding() == Continuous || flat->compounding() == Compounded);
        }
    }

    FdmBlackScholesOp::FdmBlackScholesOp(
        const ext::shared_ptr<FdmMesher>& mesher,
        const ext::shared_ptr<GeneralizedBlackScholesProcess>& bsProcess,
        Real strike,
        bool localVol,
        Real illegalLocalVolOverwrite,
        Size direction,
        ext::shared_ptr<FdmQuantoHelper> quantoHelper,
        bool constantCoefficients)
    : mesher_(mesher), rTS_(bsProcess->riskFreeRate().currentLink()),
      qTS_(bsProcess->dividendYield().currentLink()),
      volTS_(bsProcess->blackVolatility().currentLink()),
      localVol_((localVol) ? bsProcess->localVolatility().currentLink() :
                             ext::shared_ptr<LocalVolTermStructure>()),
      x_((localVol) ? Array(Exp(mesher->locations(direction))) : Array()),
      dxMap_(FirstDerivativeOp(direction, mesher)), dxxMap_(SecondDerivativeOp(direction, mesher)),
      mapT_(direction, mesher), strike_(strike),
      illegalLocalVolOverwrite_(illegalLocalVolOverwrite), direction_(direction),
      quantoHelper_(std::move(quantoHelper)),
      constantCoefficients_(constantCoefficients && localVol_ == nullptr
                            && quantoHelper_ == nullptr && isFlat(rTS_) && isFlat(qTS_)
                            && ext::dynamic_pointer_cast<BlackConstantVol>(volTS_) != nullptr) {
        // Every interval gives the same coefficients up to rounding; the unit
        // interval gives the variance exactly and the rates to about 1e-16.
        if (constantCoefficients_)
            assemble(0.0, 1.0);
    }

    void FdmBlackScholesOp::setTime(Time t1, Time t2) {
        if (!constantCoefficients_)
            assemble(t1, t2);
    }

    void FdmBlackScholesOp::assemble(Time t1, Time t2) {
        const Rate r = rTS_->forwardRate(t1, t2, Continuous).rate();
        const Rate q = qTS_->forwardRate(t1, t2, Continuous).rate();

        if (localVol_ != nullptr) {
            Array v(mesher_->layout()->size());
            for (const auto& iter : *mesher_->layout()) {
                const Size i = iter.index();

                if (illegalLocalVolOverwrite_ < 0.0) {
                    v[i] = squared(localVol_->localVol(0.5*(t1+t2), x_[i], true));
                }
                else {
                    try {
                        v[i] = squared(localVol_->localVol(0.5*(t1+t2), x_[i], true));
                    } catch (Error&) {
                        v[i] = squared(illegalLocalVolOverwrite_);
                    }
                }
            }

            if (quantoHelper_ != nullptr) {
                mapT_.axpyb(r - q - 0.5*v
                    - quantoHelper_->quantoAdjustment(Sqrt(v), t1, t2),
                    dxMap_, dxxMap_.mult(0.5*v), Array(1, -r));
            } else {
                mapT_.axpyb(r - q - 0.5*v, dxMap_,
                            dxxMap_.mult(0.5*v), Array(1, -r));
            }
        } else {
            const Real v
                = volTS_->blackForwardVariance(t1, t2, strike_)/(t2-t1);

            Real drift = r - q - 0.5*v;
            if (quantoHelper_ != nullptr)
                drift -= quantoHelper_->quantoAdjustment(std::sqrt(v), t1, t2);

            mapT_.axpyb(drift, dxMap_, 0.5*v, dxxMap_, -r);
        }
    }

    Size FdmBlackScholesOp::size() const { return 1U; }

    Array FdmBlackScholesOp::apply(const Array& u) const {
        return mapT_.apply(u);
    }

    Array FdmBlackScholesOp::apply_direction(Size direction,
                                             const Array& r) const {
        if (direction == direction_)
            return mapT_.apply(r);
        else {
            return Array(r.size(), 0.0);
        }
    }

    Array FdmBlackScholesOp::apply_mixed(const Array& r) const {
        return Array(r.size(), 0.0);
    }

    Array FdmBlackScholesOp::solve_splitting(Size direction,
                                             const Array& r, Real dt) const {
        if (direction != direction_)
            return r;
        if (!constantCoefficients_)
            return mapT_.solve_splitting(r, dt, 1.0);

        // The bands are fixed, so a step size that repeats is factorized once.
        // Sizes used only once, by steps cut short at stopping times, are
        // solved directly, which gives the same result at the same cost.
        auto f = factorizations_.find(dt);
        if (f == factorizations_.end()) {
            const bool repeated = (dt == lastDt_);
            lastDt_ = dt;
            if (!repeated)
                return mapT_.solve_splitting(r, dt, 1.0);
            f = factorizations_.emplace(dt, mapT_.factorize(dt, 1.0)).first;
        }
        return mapT_.solve_splitting(r, f->second);
    }

    Array FdmBlackScholesOp::preconditioner(const Array& r,
                                            Real dt) const {
        return solve_splitting(direction_, r, dt);
    }

    std::vector<SparseMatrix> FdmBlackScholesOp::toMatrixDecomp() const {
        return std::vector<SparseMatrix>(1, mapT_.toMatrix());
    }

}

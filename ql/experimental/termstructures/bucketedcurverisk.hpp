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

/*! \file bucketedcurverisk.hpp
    \brief swap NPVs and bucketed risk on a fast log-linear discount bootstrap
*/

#ifndef quantlib_bucketed_curve_risk_hpp
#define quantlib_bucketed_curve_risk_hpp

#include <ql/instruments/swap.hpp>
#include <ql/math/matrix.hpp>
#include <ql/patterns/lazyobject.hpp>
#include <ql/termstructures/yield/ratehelpers.hpp>

namespace QuantLib {

    //! Swap NPVs and bucketed risk on a log-linear discount bootstrap
    /*! Bootstraps the curve that PiecewiseYieldCurve<Discount, LogLinear> builds on the same
        rate helpers, values the swaps on it as DiscountingSwapEngine would, and returns their
        NPV changes when each helper quote is bumped in turn.

        Speed comes from laying out the instruments' cash flows once, at construction, as curve
        segments and interpolation weights.  Each pillar is then solved by Newton's method with
        an analytic derivative over the terms in its own segment, and a bumped quote re-solves
        only its pillar and the later ones: log-linear interpolation is local and each helper
        reads the curve up to its own pillar, so earlier pillars can't move.  The results agree
        with PiecewiseYieldCurve within its solver accuracy.

        Only single-curve setups are supported, and anything else is rejected:
        - deposit rate helpers, and swap rate helpers without spread or exogenous discounting;
        - no helper reads the curve after its pillar, as with the default pillar choice (the
          latest relevant date), except the last one, past which the curve extrapolates;
        - swap legs hold fixed-rate coupons, simple cash flows and Ibor coupons that are not in
          arrears and not fixed yet.
        The curve also forecasts the swaps' Ibor coupons, whatever their own index curves.

        \warning The instruments' dates are read at construction; build a new instance after
                 changing the evaluation date.
    */
    class BucketedCurveRisk : public LazyObject {
      public:
        BucketedCurveRisk(const Date& referenceDate,
                          std::vector<ext::shared_ptr<RateHelper>> instruments,
                          const std::vector<ext::shared_ptr<Swap>>& swaps,
                          const DayCounter& dayCounter,
                          Real accuracy = 1.0e-12);

        //! \name Curve
        //@{
        //! reference date followed by the pillar dates
        const std::vector<Date>& dates() const { return dates_; }
        //! discount factors at dates(); DiscountCurve(dates(), discounts(), ...) is the curve
        const std::vector<DiscountFactor>& discounts() const;
        //@}

        //! \name Swaps
        //@{
        //! NPVs at the reference date
        const std::vector<Real>& npvs() const;
        //! NPV changes of the swaps (columns) when each instrument quote (rows) is bumped
        Matrix bucketedRisk(Real bump = 1.0e-4) const;
        //@}

      private:
        // The discount factor at a node is exp(l[s-1] + w (l[s] - l[s-1])), with l the pillar
        // log-discounts and s >= 1 the curve segment.  The reference date has s = 0, and w > 1
        // extrapolates flat forwards past the last pillar as the curve does.
        struct Node {
            Size segment;
            Real weight;
        };
        // amount * D(node): a fixed cash flow, or the annuity of a fixed coupon
        struct Flow {
            Size node;
            Real amount;
        };
        // factor * (D(start) / D(end) - 1) * D(pay): a forecast Ibor coupon
        struct Forward {
            Size start, end, pay;
            Real factor;
        };
        struct Terms {
            std::vector<Flow> flows;
            std::vector<Forward> forwards;
            Real flowsValue(const std::vector<DiscountFactor>& d) const;
            Real forwardsValue(const std::vector<DiscountFactor>& d) const;
        };
        // A helper's implied quote is forwards / flows: a swap's floating leg over its annuity,
        // or a deposit's forward over a unit flow at the reference date.
        struct Pillar {
            Size instrument;
            Terms known, unknown;    // terms outside and inside the pillar's segment
            std::vector<Size> nodes; // nodes of the unknown terms inside the segment
        };
        struct SwapTerms {
            Terms terms;
            Size lastSegment; // bumps of later pillars can't move the NPV
        };
        struct State {
            std::vector<Real> logDiscounts;            // at the pillars
            std::vector<DiscountFactor> nodeDiscounts; // at the nodes
        };

        void performCalculations() const override;
        void bootstrap(State& state, Size from, Real bump, const State* warm) const;
        void solve(State& state, Size p, Real quote, const State* warm) const;

        Date referenceDate_, evaluationDate_;
        std::vector<ext::shared_ptr<RateHelper>> instruments_;
        Real accuracy_;
        std::vector<Date> dates_;
        std::vector<Time> times_;
        std::vector<Node> nodes_;        // one per distinct date, in date order
        std::vector<Size> segmentStart_; // segment s holds nodes [start[s], start[s + 1])
        std::vector<Pillar> pillars_;    // pillar p is solved by pillars_[p - 1]
        std::vector<SwapTerms> swaps_;
        mutable std::vector<Real> quotes_; // by pillar
        mutable State curve_;
        mutable std::vector<DiscountFactor> discounts_;
        mutable std::vector<Real> npvs_;
    };

}

#endif

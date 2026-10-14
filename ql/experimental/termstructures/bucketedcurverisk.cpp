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

#include <ql/cashflows/fixedratecoupon.hpp>
#include <ql/cashflows/iborcoupon.hpp>
#include <ql/cashflows/simplecashflow.hpp>
#include <ql/experimental/termstructures/bucketedcurverisk.hpp>
#include <ql/math/solvers1d/brent.hpp>
#include <ql/settings.hpp>
#include <ql/termstructures/yield/bootstraptraits.hpp>
#include <algorithm>
#include <numeric>

namespace QuantLib {

    BucketedCurveRisk::BucketedCurveRisk(const Date& referenceDate,
                                         std::vector<ext::shared_ptr<RateHelper>> instruments,
                                         const std::vector<ext::shared_ptr<Swap>>& swaps,
                                         const DayCounter& dayCounter,
                                         Real accuracy)
    : referenceDate_(referenceDate), evaluationDate_(Settings::instance().evaluationDate()),
      instruments_(std::move(instruments)), accuracy_(accuracy) {
        QL_REQUIRE(!instruments_.empty(), "no instruments given");
        std::vector<Size> order(instruments_.size());
        std::iota(order.begin(), order.end(), Size(0));
        std::sort(order.begin(), order.end(), [this](Size i, Size j) {
            return instruments_[i]->pillarDate() < instruments_[j]->pillarDate();
        });
        dates_.push_back(referenceDate_);
        for (Size i : order) {
            QL_REQUIRE(instruments_[i]->pillarDate() > dates_.back(),
                       "pillar dates must be distinct and after the reference date");
            dates_.push_back(instruments_[i]->pillarDate());
            registerWith(instruments_[i]);
        }
        for (const Date& d : dates_)
            times_.push_back(dayCounter.yearFraction(referenceDate_, d));

        // Terms refer to dates by their days from the reference date until all the dates are
        // known and can be turned into nodes.
        std::vector<bool> used;
        auto day = [&](const Date& d) {
            QL_REQUIRE(d >= referenceDate_, "date " << d << " before the reference date");
            auto n = static_cast<Size>(d - referenceDate_);
            if (n >= used.size())
                used.resize(n + 1, false);
            used[n] = true;
            return n;
        };
        // the cash flows yet to occur, priced as DiscountingSwapEngine does
        auto addLeg = [&](Terms& terms, const Leg& leg, Real sign) {
            for (const auto& cf : leg) {
                if (cf->hasOccurred(referenceDate_))
                    continue;
                if (auto c = ext::dynamic_pointer_cast<IborCoupon>(cf)) {
                    // pays N tau (g F + s), with the forecast F = (D(v) / D(e) - 1) / T
                    QL_REQUIRE(!c->hasFixed() && !c->isInArrears(),
                               "Ibor coupons must be neither fixed nor in arrears");
                    Real nTau = sign * c->nominal() * c->accrualPeriod();
                    terms.forwards.push_back({day(c->fixingValueDate()), day(c->fixingEndDate()),
                                              day(c->date()),
                                              nTau * c->gearing() / c->spanningTime()});
                    if (c->spread() != 0.0)
                        terms.flows.push_back({day(c->date()), nTau * c->spread()});
                } else {
                    QL_REQUIRE(ext::dynamic_pointer_cast<FixedRateCoupon>(cf) ||
                                   ext::dynamic_pointer_cast<SimpleCashFlow>(cf),
                               "only fixed-rate coupons, simple cash flows and Ibor coupons "
                               "are supported");
                    terms.flows.push_back({day(cf->date()), sign * cf->amount()});
                }
            }
        };

        std::vector<Terms> helperTerms, swapTerms;
        for (Size i : order) {
            Terms terms;
            if (auto deposit = ext::dynamic_pointer_cast<DepositRateHelper>(instruments_[i])) {
                // the index forecast (D(start) / D(end) - 1) / tau over a unit annuity
                const auto& index = deposit->iborIndex();
                Date start = deposit->earliestDate(), end = deposit->maturityDate();
                QL_REQUIRE(index->fixingDate(start) >= evaluationDate_,
                           "deposits fixing in the past are not supported");
                terms.flows.push_back({day(referenceDate_), 1.0});
                terms.forwards.push_back({day(start), day(end), day(referenceDate_),
                                          1.0 / index->dayCounter().yearFraction(start, end)});
            } else if (auto helper = ext::dynamic_pointer_cast<SwapRateHelper>(instruments_[i])) {
                QL_REQUIRE(helper->spread() == 0.0 && helper->discountingCurve().empty(),
                           "swap rate helpers with spread or exogenous discounting "
                           "are not supported");
                // the fair rate: floating leg over the fixed-leg annuity
                for (const auto& cf : helper->swap()->fixedLeg()) {
                    if (!cf->hasOccurred(referenceDate_)) {
                        auto c = ext::dynamic_pointer_cast<Coupon>(cf);
                        terms.flows.push_back({day(c->date()), c->nominal() * c->accrualPeriod()});
                    }
                }
                addLeg(terms, helper->swap()->floatingLeg(), 1.0);
            } else {
                QL_FAIL("only deposit and swap rate helpers are supported");
            }
            helperTerms.push_back(std::move(terms));
        }
        for (const auto& swap : swaps) {
            Terms terms;
            for (Size j = 0; j < swap->numberOfLegs(); ++j)
                addLeg(terms, swap->leg(j), swap->payer(j) ? -1.0 : 1.0);
            swapTerms.push_back(std::move(terms));
        }

        // nodes: the distinct dates in order, with their curve segments and weights
        std::vector<Size> node(used.size());
        for (Size n = 0; n < used.size(); ++n) {
            if (!used[n])
                continue;
            node[n] = nodes_.size();
            Date d = referenceDate_ + static_cast<Date::serial_type>(n);
            Time t = dayCounter.yearFraction(referenceDate_, d);
            // the first pillar not before t; past the last one, its segment extrapolates
            Size s = std::lower_bound(times_.begin(), times_.end() - 1, t) - times_.begin();
            nodes_.push_back({s, s == 0 ? 0.0 : (t - times_[s - 1]) / (times_[s] - times_[s - 1])});
        }
        segmentStart_.assign(times_.size() + 1, 0);
        for (const Node& x : nodes_)
            ++segmentStart_[x.segment + 1];
        std::partial_sum(segmentStart_.begin(), segmentStart_.end(), segmentStart_.begin());
        auto toNodes = [&](Terms& terms) {
            for (auto& f : terms.flows)
                f.node = node[f.node];
            for (auto& f : terms.forwards)
                f = {node[f.start], node[f.end], node[f.pay], f.factor};
        };

        // each helper's terms, split by whether they depend on its pillar
        for (Size p = 1; p < dates_.size(); ++p) {
            Terms& terms = helperTerms[p - 1];
            toNodes(terms);
            Pillar pillar{order[p - 1], {}, {}, {}};
            auto inSegment = [&](Size n) {
                QL_REQUIRE(nodes_[n].segment <= p,
                           "the helper with pillar " << dates_[p] << " reads the curve after it");
                if (nodes_[n].segment < p)
                    return false;
                pillar.nodes.push_back(n);
                return true;
            };
            for (const auto& f : terms.flows)
                (inSegment(f.node) ? pillar.unknown : pillar.known).flows.push_back(f);
            for (const auto& f : terms.forwards) {
                bool start = inSegment(f.start), end = inSegment(f.end), pay = inSegment(f.pay);
                (start || end || pay ? pillar.unknown : pillar.known).forwards.push_back(f);
            }
            std::sort(pillar.nodes.begin(), pillar.nodes.end());
            pillar.nodes.erase(std::unique(pillar.nodes.begin(), pillar.nodes.end()),
                               pillar.nodes.end());
            pillars_.push_back(std::move(pillar));
        }
        for (auto& terms : swapTerms) {
            toNodes(terms);
            // nodes are in date order, so the last one has the last segment
            Size last = 0;
            for (const auto& f : terms.flows)
                last = std::max(last, f.node);
            for (const auto& f : terms.forwards)
                last = std::max({last, f.start, f.end, f.pay});
            swaps_.push_back({std::move(terms), nodes_[last].segment});
        }
    }

    const std::vector<DiscountFactor>& BucketedCurveRisk::discounts() const {
        calculate();
        return discounts_;
    }

    const std::vector<Real>& BucketedCurveRisk::npvs() const {
        calculate();
        return npvs_;
    }

    // Each bump re-solves its pillar and the later ones, starting from the base curve, and
    // reprices the swaps that read them.
    Matrix BucketedCurveRisk::bucketedRisk(Real bump) const {
        calculate();
        Matrix risk(instruments_.size(), swaps_.size(), 0.0);
        State state = curve_;
        for (Size p = 1; p < times_.size(); ++p) {
            bootstrap(state, p, bump, &curve_);
            const Size row = pillars_[p - 1].instrument;
            const std::vector<DiscountFactor>& d = state.nodeDiscounts;
            for (Size j = 0; j < swaps_.size(); ++j) {
                const Terms& t = swaps_[j].terms;
                if (swaps_[j].lastSegment >= p)
                    risk[row][j] = t.flowsValue(d) + t.forwardsValue(d) - npvs_[j];
            }
            // restore what the next bump keeps: pillar p and its segment
            state.logDiscounts[p] = curve_.logDiscounts[p];
            std::copy(curve_.nodeDiscounts.begin() + segmentStart_[p],
                      curve_.nodeDiscounts.begin() + segmentStart_[p + 1],
                      state.nodeDiscounts.begin() + segmentStart_[p]);
        }
        return risk;
    }

    void BucketedCurveRisk::performCalculations() const {
        QL_REQUIRE(Settings::instance().evaluationDate() == evaluationDate_,
                   "the evaluation date changed after construction");
        quotes_.clear();
        for (const Pillar& p : pillars_)
            quotes_.push_back(instruments_[p.instrument]->quote()->value());
        curve_ = {std::vector<Real>(times_.size(), 0.0),
                  std::vector<DiscountFactor>(nodes_.size(), 1.0)};
        bootstrap(curve_, 1, 0.0, nullptr);
        discounts_.clear();
        for (Real l : curve_.logDiscounts)
            discounts_.push_back(std::exp(l));
        npvs_.clear();
        for (const SwapTerms& s : swaps_)
            npvs_.push_back(s.terms.flowsValue(curve_.nodeDiscounts) +
                            s.terms.forwardsValue(curve_.nodeDiscounts));
    }

    // Solves the pillars from the given one onwards, with its quote bumped; the earlier pillars
    // and their segments must be in place already.
    void BucketedCurveRisk::bootstrap(State& state, Size from, Real bump, const State* warm) const {
        for (Size p = from; p < times_.size(); ++p) {
            solve(state, p, quotes_[p - 1] + (p == from ? bump : 0.0), warm);
            const Real l0 = state.logDiscounts[p - 1], l1 = state.logDiscounts[p];
            for (Size n = segmentStart_[p]; n < segmentStart_[p + 1]; ++n)
                state.nodeDiscounts[n] = std::exp(l0 + nodes_[n].weight * (l1 - l0));
        }
    }

    void BucketedCurveRisk::solve(State& state, Size p, Real quote, const State* warm) const {
        const Pillar& pillar = pillars_[p - 1];
        std::vector<DiscountFactor>& d = state.nodeDiscounts;
        const Real l0 = state.logDiscounts[p - 1];
        const Real a0 = pillar.known.flowsValue(d), f0 = pillar.known.forwardsValue(d);

        // The quote error at the pillar log-discount y, and its derivative: only the nodes in
        // the pillar's segment depend on y, as dD/dy = w D.
        auto error = [&](Real y) {
            for (Size n : pillar.nodes)
                d[n] = std::exp(l0 + nodes_[n].weight * (y - l0));
            auto w = [&](Size n) { return nodes_[n].segment == p ? nodes_[n].weight : 0.0; };
            Real a = a0, da = 0.0, f = f0, df = 0.0;
            for (const auto& t : pillar.unknown.flows) {
                Real v = t.amount * d[t.node];
                a += v;
                da += v * w(t.node);
            }
            for (const auto& t : pillar.unknown.forwards) {
                Real r = d[t.start] / d[t.end], v = t.factor * d[t.pay];
                f += v * (r - 1.0);
                df += v * (r * (w(t.start) - w(t.end)) + (r - 1.0) * w(t.pay));
            }
            return std::make_pair(f / a - quote, (df * a - f * da) / (a * a));
        };

        // Newton's method from the warm start, if any, or from the guess of PiecewiseYieldCurve;
        // within the same bounds, and stopping like QuantLib's solvers on the discount factor.
        const Time dt = times_[p] - times_[p - 1];
        const Real yMin = l0 - detail::maxRate * dt, yMax = l0 + detail::maxRate * dt;
        Real y = warm != nullptr ? warm->logDiscounts[p] :
                 p == 1          ? -std::log(1.0 + detail::avgRate * times_[1]) :
                                   l0 * times_[p] / times_[p - 1];
        const Size maxIterations = 50;
        Real x = std::exp(y);
        for (Size i = 0; i < maxIterations; ++i) {
            auto [e, de] = error(y);
            Real yNext = y - e / de;
            if (!(yNext > yMin && yNext < yMax)) // also rejects NaN
                break;
            Real xNext = std::exp(yNext);
            bool converged = std::fabs(xNext - x) < accuracy_;
            y = yNext;
            x = xNext;
            if (converged) {
                state.logDiscounts[p] = y;
                return;
            }
        }
        // Newton left the bounds or stalled: fall back on Brent, as IterativeBootstrap uses
        Real xMin = std::exp(yMin), xMax = std::exp(yMax);
        x = Brent().solve([&](Real z) { return error(std::log(z)).first; }, accuracy_,
                          0.5 * (xMin + xMax), xMin, xMax);
        state.logDiscounts[p] = std::log(x);
    }

    Real BucketedCurveRisk::Terms::flowsValue(const std::vector<DiscountFactor>& d) const {
        Real sum = 0.0;
        for (const auto& f : flows)
            sum += f.amount * d[f.node];
        return sum;
    }

    Real BucketedCurveRisk::Terms::forwardsValue(const std::vector<DiscountFactor>& d) const {
        Real sum = 0.0;
        for (const auto& f : forwards)
            sum += f.factor * (d[f.start] / d[f.end] - 1.0) * d[f.pay];
        return sum;
    }

}

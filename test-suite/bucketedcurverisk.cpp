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

#include "toplevelfixture.hpp"
#include "utilities.hpp"
#include <ql/experimental/termstructures/bucketedcurverisk.hpp>
#include <ql/indexes/ibor/euribor.hpp>
#include <ql/instruments/makevanillaswap.hpp>
#include <ql/pricingengines/swap/discountingswapengine.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/termstructures/yield/flatforward.hpp>
#include <ql/termstructures/yield/piecewiseyieldcurve.hpp>
#include <ql/time/calendars/target.hpp>
#include <ql/time/daycounters/actual360.hpp>
#include <ql/time/daycounters/actual365fixed.hpp>
#include <ql/time/daycounters/thirty360.hpp>

using namespace QuantLib;
using namespace boost::unit_test_framework;

BOOST_FIXTURE_TEST_SUITE(QuantLibTests, TopLevelFixture)

BOOST_AUTO_TEST_SUITE(BucketedCurveRiskTests)

namespace {

    // Euribor deposits, annual 30/360 swaps against Euribor 6M, and a book of spot,
    // forward-starting, broken-date and spread swaps, one of them ending past the last
    // pillar.  The swaps are priced on a PiecewiseYieldCurve on the same helpers.
    struct Market {
        std::vector<ext::shared_ptr<SimpleQuote>> quotes;
        std::vector<ext::shared_ptr<RateHelper>> helpers;
        ext::shared_ptr<PiecewiseYieldCurve<Discount, LogLinear>> curve;
        std::vector<ext::shared_ptr<Swap>> swaps;
        std::vector<Real> nominals;

        Market(const Date& today, const std::vector<Rate>& rates) {
            Settings::instance().evaluationDate() = today;
            Calendar target = TARGET();
            auto euribor = ext::make_shared<Euribor6M>();
            const std::vector<Period> tenors = {
                1 * Days,  1 * Weeks, 1 * Months, 3 * Months, 6 * Months, 12 * Months, 2 * Years,
                3 * Years, 5 * Years, 7 * Years,  10 * Years, 15 * Years, 20 * Years,  30 * Years};
            for (Size i = 0; i < tenors.size(); ++i) {
                quotes.push_back(ext::make_shared<SimpleQuote>(rates[i]));
                Handle<Quote> quote(quotes.back());
                if (i == 0)
                    helpers.push_back(ext::make_shared<DepositRateHelper>(
                        quote, tenors[i], 0, target, Following, false, Actual360()));
                else if (tenors[i].units() != Years)
                    helpers.push_back(ext::make_shared<DepositRateHelper>(
                        quote, tenors[i], 2, target, ModifiedFollowing, true, Actual360()));
                else
                    helpers.push_back(ext::make_shared<SwapRateHelper>(
                        quote, tenors[i], target, Annual, ModifiedFollowing,
                        Thirty360(Thirty360::BondBasis), euribor));
            }
            curve = ext::make_shared<PiecewiseYieldCurve<Discount, LogLinear>>(today, helpers,
                                                                               Actual365Fixed());
            curve->enableExtrapolation();
            Handle<YieldTermStructure> handle(curve);
            auto engine = ext::make_shared<DiscountingSwapEngine>(handle);
            auto index = ext::make_shared<Euribor6M>(handle);
            auto add = [&](Real nominal, Rate rate, MakeVanillaSwap swap) {
                nominals.push_back(nominal);
                swaps.push_back(ext::shared_ptr<VanillaSwap>(
                    swap.withFixedRate(rate)
                        .withNominal(nominal)
                        .withFixedLegDayCount(Thirty360(Thirty360::BondBasis))
                        .withPricingEngine(engine)));
            };
            add(1.0e7, 0.0225, MakeVanillaSwap(5 * Years, index));
            add(2.0e7, 0.0300, MakeVanillaSwap(10 * Years, index).receiveFixed());
            add(1.0e7, 0.0285, MakeVanillaSwap(30 * Years, index));
            add(2.0e7, 0.0290, MakeVanillaSwap(5 * Years, index).withForwardStart(5 * Years));
            add(5.0e6, 0.0275,
                MakeVanillaSwap(20 * Years, index).withForwardStart(15 * Years).receiveFixed());
            add(1.0e7, 0.0255,
                MakeVanillaSwap(Period(), index)
                    .withTerminationDate(today + 7 * Years + 3 * Months + 15)
                    .withFloatingLegSpread(0.001));
        }
    };

    const std::vector<std::pair<Date, std::vector<Rate>>> scenarios = {
        {Date(13, October, 2026),
         {0.0190, 0.0192, 0.0195, 0.0201, 0.0208, 0.0215, 0.0220, 0.0228, 0.0244, 0.0257, 0.0272,
          0.0288, 0.0293, 0.0285}},
        // negative rates, with discount factors above 1
        {Date(15, June, 2020),
         {-0.0056, -0.0057, -0.0054, -0.0042, -0.0032, -0.0023, -0.0035, -0.0036, -0.0033, -0.0026,
          -0.0013, 0.0002, 0.0005, 0.0001}},
        // Christmas holidays between evaluation and spot
        {Date(23, December, 2025),
         {0.0193, 0.0194, 0.0196, 0.0203, 0.0210, 0.0217, 0.0222, 0.0230, 0.0246, 0.0259, 0.0274,
          0.0290, 0.0295, 0.0287}},
    };

}

BOOST_AUTO_TEST_CASE(testAgainstPiecewiseYieldCurve) {
    BOOST_TEST_MESSAGE("Testing bucketed curve risk against PiecewiseYieldCurve...");

    // PiecewiseYieldCurve stops within its accuracy (1e-12) of each discount factor, and
    // BucketedCurveRisk well inside it.  An NPV per unit nominal moves by a few times that
    // error, so 1e-11 bounds both the discount factors and the NPVs and risks per nominal.
    const Real discountTolerance = 1.0e-11, npvTolerance = 1.0e-11;
    const Real bump = 1.0e-4;

    for (const auto& [today, rates] : scenarios) {
        Market m(today, rates);
        BucketedCurveRisk risk(today, m.helpers, m.swaps, Actual365Fixed());

        BOOST_CHECK(risk.dates() == m.curve->dates());
        for (Size i = 0; i < risk.dates().size(); ++i) {
            Real expected = m.curve->data()[i], calculated = risk.discounts()[i];
            if (std::fabs(calculated - expected) > discountTolerance)
                BOOST_ERROR("discount factor mismatch on " << risk.dates()[i] << " for " << today
                                                           << ":\n    expected:   " << expected
                                                           << "\n    calculated: " << calculated);
        }

        std::vector<Real> npvs;
        for (Size j = 0; j < m.swaps.size(); ++j) {
            npvs.push_back(m.swaps[j]->NPV());
            if (std::fabs(risk.npvs()[j] - npvs[j]) > npvTolerance * m.nominals[j])
                BOOST_ERROR("NPV mismatch for swap " << j << " on " << today
                                                     << ":\n    expected:   " << npvs[j]
                                                     << "\n    calculated: " << risk.npvs()[j]);
        }

        Matrix calculated = risk.bucketedRisk(bump);
        for (Size k = 0; k < m.quotes.size(); ++k) {
            Real quote = m.quotes[k]->value();
            m.quotes[k]->setValue(quote + bump);
            for (Size j = 0; j < m.swaps.size(); ++j) {
                Real expected = m.swaps[j]->NPV() - npvs[j];
                if (std::fabs(calculated[k][j] - expected) > npvTolerance * m.nominals[j])
                    BOOST_ERROR("risk mismatch for swap "
                                << j << " and quote " << k << " on " << today
                                << ":\n    expected:   " << expected
                                << "\n    calculated: " << calculated[k][j]);
            }
            m.quotes[k]->setValue(quote);
        }
    }
}

BOOST_AUTO_TEST_CASE(testQuoteChanges) {
    BOOST_TEST_MESSAGE("Testing bucketed curve risk against changed quotes...");

    // Moving a quote triggers a full bootstrap, which converges to the same curve as the
    // partial one of the bucketed risk up to round-off.
    const Real tolerance = 1.0e-13, bump = 1.0e-4;

    const auto& [today, rates] = scenarios.front();
    Market m(today, rates);
    BucketedCurveRisk risk(today, m.helpers, m.swaps, Actual365Fixed());
    const std::vector<Real> npvs = risk.npvs();
    Matrix expected = risk.bucketedRisk(bump);

    for (Size k = 0; k < m.quotes.size(); ++k) {
        Real quote = m.quotes[k]->value();
        m.quotes[k]->setValue(quote + bump);
        for (Size j = 0; j < m.swaps.size(); ++j) {
            Real calculated = risk.npvs()[j] - npvs[j];
            if (std::fabs(calculated - expected[k][j]) > tolerance * m.nominals[j])
                BOOST_ERROR("NPV change mismatch for swap "
                            << j << " and quote " << k << ":\n    expected:   " << expected[k][j]
                            << "\n    calculated: " << calculated);
        }
        m.quotes[k]->setValue(quote);
    }
}

BOOST_AUTO_TEST_CASE(testUnsupportedInputs) {
    BOOST_TEST_MESSAGE("Testing bucketed curve risk on unsupported inputs...");

    const Date& today = scenarios.front().first;
    const std::vector<Rate>& rates = scenarios.front().second;
    Market m(today, rates);
    auto euribor = ext::make_shared<Euribor6M>();
    auto swapHelper = [&](const Handle<YieldTermStructure>& discount, Pillar::Choice pillar,
                          const Date& pillarDate) {
        return ext::make_shared<SwapRateHelper>(
            rates[rates.size() - 2], 20 * Years, TARGET(), Annual, ModifiedFollowing,
            Thirty360(Thirty360::BondBasis), euribor, Handle<Quote>(), 0 * Days, discount,
            Null<Natural>(), pillar, pillarDate);
    };
    // in place of the 20-year swap helper
    auto checkRejected = [&](const ext::shared_ptr<RateHelper>& helper, const std::string& error) {
        auto helpers = m.helpers;
        helpers[helpers.size() - 2] = helper;
        BOOST_CHECK_EXCEPTION(BucketedCurveRisk(today, helpers, m.swaps, Actual365Fixed()), Error,
                              ExpectedErrorMessage(error));
    };

    checkRejected(ext::make_shared<FraRateHelper>(0.02, 9 * Months, euribor),
                  "only deposit and swap rate helpers");
    // a pillar before the latest relevant date, so that the helper reads the next segment
    Date pillar = m.helpers[m.helpers.size() - 2]->latestRelevantDate() - 3 * Months;
    checkRejected(swapHelper({}, Pillar::CustomDate, pillar), "reads the curve after it");
    Handle<YieldTermStructure> ois(ext::make_shared<FlatForward>(today, 0.02, Actual365Fixed()));
    checkRejected(swapHelper(ois, Pillar::LastRelevantDate, {}), "exogenous discounting");

    // the instruments' dates move with the evaluation date
    BucketedCurveRisk risk(today, m.helpers, m.swaps, Actual365Fixed());
    Settings::instance().evaluationDate() = today + 1;
    BOOST_CHECK_EXCEPTION(risk.npvs(), Error, ExpectedErrorMessage("evaluation date changed"));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE_END()

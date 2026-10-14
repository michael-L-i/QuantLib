/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
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

#include "toplevelfixture.hpp"
#include "utilities.hpp"
#include <ql/exercise.hpp>
#include <ql/experimental/processes/extendedblackscholesprocess.hpp>
#include <ql/instruments/basketoption.hpp>
#include <ql/instruments/vanillaoption.hpp>
#include <ql/methods/montecarlo/mctraits.hpp>
#include <ql/pricingengines/basket/mceuropeanbasketengine.hpp>
#include <ql/pricingengines/vanilla/mceuropeanengine.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/processes/geometricbrownianprocess.hpp>
#include <ql/processes/ornsteinuhlenbeckprocess.hpp>
#include <ql/processes/squarerootprocess.hpp>
#include <ql/processes/stochasticprocessarray.hpp>
#include <ql/termstructures/volatility/equityfx/blackvariancecurve.hpp>
#include <ql/termstructures/yield/zerocurve.hpp>
#include <ql/time/daycounters/actual360.hpp>
#include <ql/time/daycounters/actual365fixed.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/utilities/dataformatters.hpp>

using namespace QuantLib;
using namespace boost::unit_test_framework;

BOOST_FIXTURE_TEST_SUITE(QuantLibTests, TopLevelFixture)

BOOST_AUTO_TEST_SUITE(PathGeneratorTests)

void testSingle(const ext::shared_ptr<StochasticProcess1D>& process,
                const std::string& tag, bool brownianBridge,
                Real expected, Real antithetic) {
    typedef PseudoRandom::rsg_type rsg_type;
    typedef PathGenerator<rsg_type>::sample_type sample_type;

    BigNatural seed = 42;
    Time length = 10;
    Size timeSteps = 12;
    rsg_type rsg = PseudoRandom::make_sequence_generator(timeSteps, seed);
    PathGenerator<rsg_type> generator(process, length, timeSteps,
                                      rsg, brownianBridge);
    Size i;
    for (i=0; i<100; i++)
        generator.next();

    sample_type sample = generator.next();
    Real calculated = sample.value.back();
    Real error = std::fabs(calculated-expected);
    Real tolerance = 2.0e-8;
    if (error > tolerance) {
        BOOST_ERROR("using " << tag << " process "
                    << (brownianBridge ? "with " : "without ")
                    << "brownian bridge:\n"
                    << std::setprecision(13)
                    << "    calculated: " << calculated << "\n"
                    << "    expected:   " << expected << "\n"
                    << "    error:      " << error << "\n"
                    << "    tolerance:  " << tolerance);
    }

    sample = generator.antithetic();
    calculated = sample.value.back();
    error = std::fabs(calculated-antithetic);
    tolerance = 2.0e-7;
    if (error > tolerance) {
        BOOST_ERROR("using " << tag << " process "
                    << (brownianBridge ? "with " : "without ")
                    << "brownian bridge:\n"
                    << "antithetic sample:\n"
                    << std::setprecision(13)
                    << "    calculated: " << calculated << "\n"
                    << "    expected:   " << antithetic << "\n"
                    << "    error:      " << error << "\n"
                    << "    tolerance:  " << tolerance);
    }

}

void testMultiple(const ext::shared_ptr<StochasticProcess>& process,
                  const std::string& tag,
                  Real expected[], Real antithetic[]) {
    typedef PseudoRandom::rsg_type rsg_type;
    typedef MultiPathGenerator<rsg_type>::sample_type sample_type;

    BigNatural seed = 42;
    Time length = 10;
    Size timeSteps = 12;
    Size assets = process->size();
    rsg_type rsg = PseudoRandom::make_sequence_generator(timeSteps*assets,
                                                         seed);
    MultiPathGenerator<rsg_type> generator(process,
                                           TimeGrid(length, timeSteps),
                                           rsg, false);
    Size i, j;
    for (i=0; i<100; i++)
        generator.next();

    sample_type sample = generator.next();
    Array calculated(assets);
    Real error, tolerance = 2.0e-7;
    for (j=0; j<assets; j++)
        calculated[j] = sample.value[j].back();
    for (j=0; j<assets; j++) {
        error = std::fabs(calculated[j]-expected[j]);
        if (error > tolerance) {
            BOOST_ERROR("using " << tag << " process "
                        << "(" << io::ordinal(j+1) << " asset:)\n"
                        << std::setprecision(13)
                        << "    calculated: " << calculated[j] << "\n"
                        << "    expected:   " << expected[j] << "\n"
                        << "    error:      " << error << "\n"
                        << "    tolerance:  " << tolerance);
        }
    }

    sample = generator.antithetic();
    for (j=0; j<assets; j++)
        calculated[j] = sample.value[j].back();
    for (j=0; j<assets; j++) {
        error = std::fabs(calculated[j]-antithetic[j]);
        if (error > tolerance) {
            BOOST_ERROR("using " << tag << " process "
                        << "(" << io::ordinal(j+1) << " asset:)\n"
                        << "antithetic sample:\n"
                        << std::setprecision(13)
                        << "    calculated: " << calculated[j] << "\n"
                        << "    expected:   " << antithetic[j] << "\n"
                        << "    error:      " << error << "\n"
                        << "    tolerance:  " << tolerance);
        }
    }
}


BOOST_AUTO_TEST_CASE(testPathGenerator) {

    BOOST_TEST_MESSAGE("Testing 1-D path generation against cached values...");

    Settings::instance().evaluationDate() = Date(26,April,2005);

    Handle<Quote> x0(ext::shared_ptr<Quote>(new SimpleQuote(100.0)));
    Handle<YieldTermStructure> r(flatRate(0.05, Actual360()));
    Handle<YieldTermStructure> q(flatRate(0.02, Actual360()));
    Handle<BlackVolTermStructure> sigma(flatVol(0.20, Actual360()));
    // commented values must be used when Halley's correction is enabled
    testSingle(ext::shared_ptr<StochasticProcess1D>(
                                 new BlackScholesMertonProcess(x0,q,r,sigma)),
               "Black-Scholes", false, 26.13784357783, 467.2928561411);
                                    // 26.13784357783, 467.2928562519);
    testSingle(ext::shared_ptr<StochasticProcess1D>(
                                 new BlackScholesMertonProcess(x0,q,r,sigma)),
               "Black-Scholes", true, 60.28215549393, 202.6143139999);
                                   // 60.28215551021, 202.6143139437);

    testSingle(ext::shared_ptr<StochasticProcess1D>(
                       new GeometricBrownianMotionProcess(100.0, 0.03, 0.20)),
               "geometric Brownian", false, 27.62223714065, 483.6026514084);
                                         // 27.62223714065, 483.602651493);

    testSingle(ext::shared_ptr<StochasticProcess1D>(
                                     new OrnsteinUhlenbeckProcess(0.1, 0.20)),
               "Ornstein-Uhlenbeck", false, -0.8372003433557, 0.8372003433557);

    testSingle(ext::shared_ptr<StochasticProcess1D>(
                                 new SquareRootProcess(0.1, 0.1, 0.20, 10.0)),
               "square-root", false, 1.70608664108, 6.024200546031);
}

BOOST_AUTO_TEST_CASE(testMultiPathGenerator) {

    BOOST_TEST_MESSAGE("Testing n-D path generation against cached values...");

    Settings::instance().evaluationDate() = Date(26,April,2005);

    Handle<Quote> x0(ext::shared_ptr<Quote>(new SimpleQuote(100.0)));
    Handle<YieldTermStructure> r(flatRate(0.05, Actual360()));
    Handle<YieldTermStructure> q(flatRate(0.02, Actual360()));
    Handle<BlackVolTermStructure> sigma(flatVol(0.20, Actual360()));

    Matrix correlation(3,3);
    correlation[0][0] = 1.0; correlation[0][1] = 0.9; correlation[0][2] = 0.7;
    correlation[1][0] = 0.9; correlation[1][1] = 1.0; correlation[1][2] = 0.4;
    correlation[2][0] = 0.7; correlation[2][1] = 0.4; correlation[2][2] = 1.0;

    std::vector<ext::shared_ptr<StochasticProcess1D> > processes(3);
    ext::shared_ptr<StochasticProcess> process;

    processes[0] = ext::shared_ptr<StochasticProcess1D>(
                                 new BlackScholesMertonProcess(x0,q,r,sigma));
    processes[1] = ext::shared_ptr<StochasticProcess1D>(
                                 new BlackScholesMertonProcess(x0,q,r,sigma));
    processes[2] = ext::shared_ptr<StochasticProcess1D>(
                                 new BlackScholesMertonProcess(x0,q,r,sigma));
    process = ext::shared_ptr<StochasticProcess>(
                           new StochasticProcessArray(processes,correlation));
    // commented values must be used when Halley's correction is enabled
    Real result1[] = {
        188.2235868185,
        270.6713069569,
        113.0431145652 };
    // Real result1[] = {
    //     188.2235869273,
    //     270.6713071508,
    //     113.0431145652 };
    Real result1a[] = {
        64.89105742957,
        45.12494404804,
        108.0475146914 };
    // Real result1a[] = {
    //     64.89105739157,
    //     45.12494401537,
    //     108.0475146914 };
    testMultiple(process, "Black-Scholes", result1, result1a);

    processes[0] = ext::shared_ptr<StochasticProcess1D>(
                       new GeometricBrownianMotionProcess(100.0, 0.03, 0.20));
    processes[1] = ext::shared_ptr<StochasticProcess1D>(
                       new GeometricBrownianMotionProcess(100.0, 0.03, 0.20));
    processes[2] = ext::shared_ptr<StochasticProcess1D>(
                       new GeometricBrownianMotionProcess(100.0, 0.03, 0.20));
    process = ext::shared_ptr<StochasticProcess>(
                           new StochasticProcessArray(processes,correlation));
    Real result2[] = {
        174.8266131680,
        237.2692443633,
        119.1168555440 };
    // Real result2[] = {
    //     174.8266132344,
    //     237.2692444869,
    //     119.1168555605 };
    Real result2a[] = {
        57.69082393020,
        38.50016862915,
        116.4056510107 };
    // Real result2a[] = {
    //     57.69082387657,
    //     38.50016858691,
    //     116.4056510107 };
    testMultiple(process, "geometric Brownian", result2, result2a);

    processes[0] = ext::shared_ptr<StochasticProcess1D>(
                                     new OrnsteinUhlenbeckProcess(0.1, 0.20));
    processes[1] = ext::shared_ptr<StochasticProcess1D>(
                                     new OrnsteinUhlenbeckProcess(0.1, 0.20));
    processes[2] = ext::shared_ptr<StochasticProcess1D>(
                                     new OrnsteinUhlenbeckProcess(0.1, 0.20));
    process = ext::shared_ptr<StochasticProcess>(
                           new StochasticProcessArray(processes,correlation));
    Real result3[] = {
        0.2942058437284,
        0.5525006418386,
        0.02650931054575 };
    Real result3a[] = {
        -0.2942058437284,
        -0.5525006418386,
        -0.02650931054575 };
    testMultiple(process, "Ornstein-Uhlenbeck", result3, result3a);

    processes[0] = ext::shared_ptr<StochasticProcess1D>(
                                 new SquareRootProcess(0.1, 0.1, 0.20, 10.0));
    processes[1] = ext::shared_ptr<StochasticProcess1D>(
                                 new SquareRootProcess(0.1, 0.1, 0.20, 10.0));
    processes[2] = ext::shared_ptr<StochasticProcess1D>(
                                 new SquareRootProcess(0.1, 0.1, 0.20, 10.0));
    process = ext::shared_ptr<StochasticProcess>(
                           new StochasticProcessArray(processes,correlation));
    Real result4[] = {
        4.279510844897,
        4.943783503533,
        3.590930385958 };
    Real result4a[] = {
        2.763967737724,
        2.226487196647,
        3.503859264341 };
    testMultiple(process, "square-root", result4, result4a);
}

BOOST_AUTO_TEST_CASE(testProcessArrayWithWrongDimension) {

    BOOST_TEST_MESSAGE("Testing that a process array rejects variates of the wrong size...");

    std::vector<ext::shared_ptr<StochasticProcess1D> > processes(
        2, ext::make_shared<GeometricBrownianMotionProcess>(100.0, 0.03, 0.20));
    Matrix correlation(2, 2, 0.5);
    correlation[0][0] = correlation[1][1] = 1.0;
    StochasticProcessArray process(processes, correlation);

    Array x0 = process.initialValues();
    BOOST_CHECK_THROW(process.evolve(0.0, x0, 0.1, Array(1, 0.1)), Error);
    BOOST_CHECK_THROW(process.evolve(0.0, x0, 0.1, Array(3, 0.1)), Error);
}

namespace {

    // Derived classes that path generators don't recognize, so their
    // steps call evolve() instead of using precomputed values.
    class DerivedBlackScholesMertonProcess : public BlackScholesMertonProcess {
      public:
        using BlackScholesMertonProcess::BlackScholesMertonProcess;
    };

    class DerivedProcessArray : public StochasticProcessArray {
      public:
        using StochasticProcessArray::StochasticProcessArray;
    };

    class ScaledProcessArray : public StochasticProcessArray {
      public:
        using StochasticProcessArray::StochasticProcessArray;
        Array evolve(Time t0, const Array& x0, Time dt, const Array& dw) const override {
            return 2.0 * StochasticProcessArray::evolve(t0, x0, dt, dw);
        }
    };

    const Matrix correlation = {{1.0, 0.5}, {0.5, 1.0}};

    // one-year options priced by MC engines with fixed settings
    ext::shared_ptr<Instrument>
    europeanOption(const ext::shared_ptr<GeneralizedBlackScholesProcess>& process) {
        Date today = Settings::instance().evaluationDate();
        auto option = ext::make_shared<VanillaOption>(
            ext::make_shared<PlainVanillaPayoff>(Option::Call, 100.0),
            ext::make_shared<EuropeanExercise>(today + 1 * Years));
        option->setPricingEngine(MakeMCEuropeanEngine<PseudoRandom>(process)
                                     .withSteps(10)
                                     .withSamples(1023)
                                     .withAntitheticVariate()
                                     .withSeed(42));
        return option;
    }

    ext::shared_ptr<Instrument> basketOption(const ext::shared_ptr<StochasticProcessArray>& process) {
        Date today = Settings::instance().evaluationDate();
        auto option = ext::make_shared<BasketOption>(
            ext::make_shared<AverageBasketPayoff>(
                ext::make_shared<PlainVanillaPayoff>(Option::Call, 100.0), process->size()),
            ext::make_shared<EuropeanExercise>(today + 1 * Years));
        option->setPricingEngine(MakeMCEuropeanBasketEngine<PseudoRandom>(process)
                                     .withSteps(10)
                                     .withSamples(1023)
                                     .withAntitheticVariate()
                                     .withSeed(42));
        return option;
    }

}

BOOST_AUTO_TEST_CASE(testEnginesWithPrecomputedSteps) {

    BOOST_TEST_MESSAGE("Testing MC engines with precomputed steps against evolve()...");

    // time-dependent drifts and variances
    Date today = Settings::instance().evaluationDate();
    DayCounter dc = Actual365Fixed();
    std::vector<Date> dates = {today, today + 6 * Months, today + 3 * Years};
    Handle<YieldTermStructure> r(
        ext::make_shared<ZeroCurve>(dates, std::vector<Rate>{0.01, 0.03, 0.04}, dc));
    Handle<YieldTermStructure> q(
        ext::make_shared<ZeroCurve>(dates, std::vector<Rate>{0.02, 0.01, 0.0}, dc));
    std::vector<Handle<BlackVolTermStructure>> vols = {
        Handle<BlackVolTermStructure>(flatVol(today, 0.20, dc)),
        Handle<BlackVolTermStructure>(ext::make_shared<BlackVarianceCurve>(
            today, std::vector<Date>(dates.begin() + 1, dates.end()),
            std::vector<Volatility>{0.30, 0.25}, dc))};

    std::vector<ext::shared_ptr<StochasticProcess1D>> processes, derived;
    for (const auto& vol : vols) {
        Handle<Quote> s(ext::make_shared<SimpleQuote>(100.0));
        auto process = ext::make_shared<BlackScholesMertonProcess>(s, q, r, vol);
        auto other = ext::make_shared<DerivedBlackScholesMertonProcess>(s, q, r, vol);
        BOOST_CHECK_EQUAL(europeanOption(process)->NPV(), europeanOption(other)->NPV());
        processes.push_back(process);
        derived.push_back(other);
    }
    BOOST_CHECK_EQUAL(
        basketOption(ext::make_shared<StochasticProcessArray>(processes, correlation))->NPV(),
        basketOption(ext::make_shared<DerivedProcessArray>(derived, correlation))->NPV());
}

BOOST_AUTO_TEST_CASE(testPrecomputedStepsFollowQuotes) {

    BOOST_TEST_MESSAGE("Testing that MC prices with precomputed steps follow quote changes...");

    Date today = Settings::instance().evaluationDate();
    DayCounter dc = Actual365Fixed();
    auto rate = ext::make_shared<SimpleQuote>(0.03);
    auto vol = ext::make_shared<SimpleQuote>(0.20);
    auto makeOptions = [&]() {
        auto process = ext::make_shared<BlackScholesMertonProcess>(
            Handle<Quote>(ext::make_shared<SimpleQuote>(100.0)),
            Handle<YieldTermStructure>(flatRate(today, 0.0, dc)),
            Handle<YieldTermStructure>(flatRate(today, rate, dc)),
            Handle<BlackVolTermStructure>(flatVol(today, vol, dc)));
        std::vector<ext::shared_ptr<StochasticProcess1D>> processes(2, process);
        return std::vector<ext::shared_ptr<Instrument>>{
            europeanOption(process),
            basketOption(ext::make_shared<StochasticProcessArray>(processes, correlation))};
    };

    auto options = makeOptions();
    for (const auto& [quote, value] : {std::make_pair(vol, 0.30), std::make_pair(rate, 0.05)}) {
        std::vector<Real> before;
        for (const auto& option : options)
            before.push_back(option->NPV());
        quote->setValue(value);
        auto fresh = makeOptions();
        for (Size i = 0; i < options.size(); ++i) {
            BOOST_CHECK(options[i]->NPV() != before[i]);
            BOOST_CHECK_EQUAL(options[i]->NPV(), fresh[i]->NPV());
        }
    }
}

BOOST_AUTO_TEST_CASE(testProcessesOverridingEvolve) {

    BOOST_TEST_MESSAGE("Testing path generation for processes overriding evolve()...");

    typedef PseudoRandom::rsg_type rsg_type;
    TimeGrid grid(2.0, 8);
    Size steps = grid.size() - 1;

    // compares the first path to the one obtained by calling evolve()
    auto check1D = [&](const ext::shared_ptr<StochasticProcess1D>& process) {
        rsg_type rsg = PseudoRandom::make_sequence_generator(steps, 42), copy = rsg;
        PathGenerator<rsg_type> generator(process, grid, rsg, false);
        const Path& path = generator.next().value;
        const auto& dw = copy.nextSequence().value;
        Real x = process->x0();
        for (Size i = 0; i < steps; ++i) {
            x = process->evolve(grid[i], x, grid.dt(i), dw[i]);
            BOOST_CHECK_EQUAL(path[i + 1], x);
        }
    };
    auto checkND = [&](const ext::shared_ptr<StochasticProcess>& process) {
        Size n = process->factors();
        rsg_type rsg = PseudoRandom::make_sequence_generator(n * steps, 42), copy = rsg;
        MultiPathGenerator<rsg_type> generator(process, grid, rsg);
        const MultiPath& path = generator.next().value;
        const auto& dw = copy.nextSequence().value;
        Array x = process->initialValues();
        for (Size i = 0; i < steps; ++i) {
            x = process->evolve(grid[i], x, grid.dt(i),
                                Array(dw.begin() + i * n, dw.begin() + (i + 1) * n));
            for (Size j = 0; j < x.size(); ++j)
                BOOST_CHECK_EQUAL(path[j][i + 1], x[j]);
        }
    };

    Date today = Settings::instance().evaluationDate();
    DayCounter dc = Actual365Fixed();
    Handle<Quote> s(ext::make_shared<SimpleQuote>(100.0));
    Handle<YieldTermStructure> r(flatRate(today, 0.05, dc)), q(flatRate(today, 0.02, dc));
    Handle<BlackVolTermStructure> v(flatVol(today, 0.20, dc));
    std::vector<ext::shared_ptr<StochasticProcess1D>> extended(
        2, ext::make_shared<ExtendedBlackScholesMertonProcess>(s, q, r, v));
    std::vector<ext::shared_ptr<StochasticProcess1D>> bsm(
        2, ext::make_shared<BlackScholesMertonProcess>(s, q, r, v));

    check1D(extended[0]);
    checkND(ext::make_shared<StochasticProcessArray>(extended, correlation));
    checkND(ext::make_shared<ScaledProcessArray>(bsm, correlation));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE_END()

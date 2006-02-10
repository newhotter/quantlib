/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2005, 2006 Klaus Spanderen

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it
 under the terms of the QuantLib license.  You should have received a
 copy of the license along with this program; if not, please email
 <quantlib-dev@lists.sf.net>. The license is also available online at
 <http://quantlib.org/reference/license.html>.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

#include "libormarketmodelprocess.hpp"
#include "utilities.hpp"

#include <ql/timegrid.hpp>
#include <ql/RandomNumbers/rngtraits.hpp>
#include <ql/MonteCarlo/multipathgenerator.hpp>
#include <ql/Indexes/euribor.hpp>
#include <ql/Math/generalstatistics.hpp>
#include <ql/TermStructures/zerocurve.hpp>
#include <ql/DayCounters/actualactual.hpp>
#include <ql/Volatilities/capletconstantvol.hpp>
#include <ql/Volatilities/capletvariancecurve.hpp>
#include <ql/Processes/lfmhullwhiteparam.hpp>

using namespace QuantLib;
using namespace boost::unit_test_framework;

QL_BEGIN_TEST_LOCALS(LiborMarketModelProcessTest)

Size len = 10;

boost::shared_ptr<Xibor> makeIndex() {
    DayCounter dayCounter = Actual360();
    std::vector<Date> dates;
    std::vector<Rate> rates;
    dates.push_back(Date(4,September,2005));
    dates.push_back(Date(4,September,2018));
    rates.push_back(0.01);
    rates.push_back(0.08);

    Handle<YieldTermStructure> termStructure(
                      boost::shared_ptr<YieldTermStructure>(
                                      new ZeroCurve(dates,rates,dayCounter)));

    boost::shared_ptr<Xibor> index(new Euribor(1, Years, termStructure));

    Date todaysDate = index->calendar().adjust(Date(4,September,2005));
    Settings::instance().evaluationDate() = todaysDate;

    dates[0] = index->calendar().advance(todaysDate,
                                         index->settlementDays(), Days);

    termStructure.linkTo(boost::shared_ptr<YieldTermStructure>(
                                    new ZeroCurve(dates, rates, dayCounter)));

    return index;
}


boost::shared_ptr<CapletVarianceCurve>
makeCapVolCurve(const Date& todaysDate) {
    Volatility vols[] = {14.40, 17.15, 16.81, 16.64, 16.17,
                         15.78, 15.40, 15.21, 14.86, 14.54};

    std::vector<Date> dates;
    std::vector<Volatility> capletVols;
    boost::shared_ptr<LiborForwardModelProcess> process(
        new LiborForwardModelProcess(len+1, makeIndex()));

    for (Size i=0; i < len; ++i) {
        capletVols.push_back(vols[i]/100);
        dates.push_back(process->fixingDates()[i+1]);
    }

    return boost::shared_ptr<CapletVarianceCurve>(
                         new CapletVarianceCurve(todaysDate, dates,
                                                 capletVols, ActualActual()));
}

boost::shared_ptr<LiborForwardModelProcess>
makeProcess(const Matrix& volaComp = Matrix()) {
    Size factors = (volaComp.empty() ? 1 : volaComp.columns());

    boost::shared_ptr<Xibor> index = makeIndex();
    boost::shared_ptr<LiborForwardModelProcess> process(
        new LiborForwardModelProcess(len, index));

    boost::shared_ptr<LfmCovarianceParameterization> fct(
        new LfmHullWhiteParameterization(
            process,
            makeCapVolCurve(Settings::instance().evaluationDate()),
            volaComp * transpose(volaComp), factors));

    process->setCovarParam(fct);

    return process;
}

void teardown() {
    Settings::instance().evaluationDate() = Date();
}

QL_END_TEST_LOCALS(LiborMarketModelProcessTest)


void LiborMarketModelProcessTest::testInitialisation() {
    BOOST_MESSAGE("Testing caplet LMM process initialisation...");

    QL_TEST_BEGIN

    DayCounter dayCounter = Actual360();
    Handle<YieldTermStructure> termStructure(
        flatRate(Date::todaysDate(), 0.04, dayCounter));

    boost::shared_ptr<Xibor> index(new Euribor(6, Months, termStructure));
    boost::shared_ptr<CapletVolatilityStructure> capletVol(
        new CapletConstantVolatility(termStructure->referenceDate(), 0.2,
                                     termStructure->dayCounter()));

    Calendar calendar = index->calendar();

    for (Integer daysOffset=0; daysOffset < 1825 /* 5 year*/; daysOffset+=8) {
        Date todaysDate = calendar.adjust(Date::todaysDate()+daysOffset);
        Settings::instance().evaluationDate() = todaysDate;
        Date settlementDate =
            calendar.advance(todaysDate, index->settlementDays(), Days);

        termStructure.linkTo(flatRate(settlementDate, 0.04, dayCounter));

        LiborForwardModelProcess process(60, index);

        std::vector<Time> fixings = process.fixingTimes();
        for (Size i=1; i < fixings.size()-1; ++i) {
            Size ileft  = process.nextIndexReset(fixings[i]-0.000001);
            Size iright = process.nextIndexReset(fixings[i]+0.000001);
            Size ii     = process.nextIndexReset(fixings[i]);

            if ((ileft != i) || (iright != i+1) || (ii != i+1)) {
                BOOST_ERROR("Failed to next index resets");
            }
        }

    }

    QL_TEST_TEARDOWN
}

void LiborMarketModelProcessTest::testLambdaBootstrapping() {
    BOOST_MESSAGE("Testing caplet-LMM lambda bootstrapping...");

    QL_TEST_BEGIN

    Real tolerance = 1e-10;
    Volatility lambdaExpected[]= {14.3010297550, 19.3821411939, 15.9816590141,
                                  15.9953118303, 14.0570815635, 13.5687599894,
                                  12.7477197786, 13.7056638165, 11.6191989567};

    boost::shared_ptr<LiborForwardModelProcess> process = makeProcess();

    Matrix covar = process->covariance(0.0, Null<Array>(), 1.0);

    for (Size i=0; i<9; ++i) {
        const Real calculated = std::sqrt(covar[i+1][i+1]);
        const Real expected   = lambdaExpected[i]/100;

        if (std::fabs(calculated - expected) > tolerance)
            BOOST_ERROR("Failed to reproduce expected lambda values"
                        << "\n    calculated: " << calculated
                        << "\n    expected:   " << expected);
    }

    boost::shared_ptr<LfmCovarianceParameterization> param =
        process->covarParam();

    std::vector<Time> tmp = process->fixingTimes();
    TimeGrid grid(tmp.begin(), tmp.end(), 14);

    for (Size t=0; t<grid.size(); ++t) {
        Matrix diff = (param->integratedCovariance(grid[t])
        - param->LfmCovarianceParameterization::integratedCovariance(grid[t]));

        for (Size i=0; i<diff.rows(); ++i) {
            for (Size j=0; j<diff.columns(); ++j) {
                if (std::fabs(diff[i][j]) > tolerance) {
                     BOOST_ERROR("Failed to reproduce integrated covariance"
                        << "\n    calculated: " << diff[i][j]
                        << "\n    expected:   " << 0);
                }
            }
        }
    }

    QL_TEST_TEARDOWN
}

void LiborMarketModelProcessTest::testMonteCarloCapletPricing() {
    BOOST_MESSAGE("Testing caplet-LMM Monte-Carlo caplet pricing...");

    QL_TEST_BEGIN

    /* factor loadings are taken from Hull & White article
       plus extra normalisation to get orthogonal eigenvectors
       http://www.rotman.utoronto.ca/~amackay/fin/libormktmodel2.pdf */
    Real compValues[] = {0.85549771, 0.46707264, 0.22353259,
                         0.91915359, 0.37716089, 0.11360610,
                         0.96438280, 0.26413316,-0.01412414,
                         0.97939148, 0.13492952,-0.15028753,
                         0.95970595,-0.00000000,-0.28100621,
                         0.97939148,-0.13492952,-0.15028753,
                         0.96438280,-0.26413316,-0.01412414,
                         0.91915359,-0.37716089, 0.11360610,
                         0.85549771,-0.46707264, 0.22353259};

    Matrix volaComp(9,3);
    std::copy(compValues, compValues+9*3, volaComp.begin());

    boost::shared_ptr<LiborForwardModelProcess> process1 = makeProcess();
    boost::shared_ptr<LiborForwardModelProcess> process2 = makeProcess(
                                                                    volaComp);
    std::vector<Time> tmp = process1->fixingTimes();
    TimeGrid grid(tmp.begin(), tmp.end(),12);

    Size i;
    std::vector<Size> location;
    for (i=0; i < tmp.size(); ++i) {
        location.push_back(
                      std::find(grid.begin(),grid.end(),tmp[i])-grid.begin());
    }

    // set-up a small Monte-Carlo simulation to price caplets
    // and ratchet caps using a one- and a three factor libor market model
    typedef LowDiscrepancy::rsg_type rsg_type;
    typedef MultiPathGenerator<rsg_type>::sample_type sample_type;

    BigNatural seed = 42;
    rsg_type rsg1 = LowDiscrepancy::make_sequence_generator(
                            process1->factors()*(grid.size()-1), seed);
    rsg_type rsg2 = LowDiscrepancy::make_sequence_generator(
                            process2->factors()*(grid.size()-1), seed);
    MultiPathGenerator<rsg_type> generator1(process1, grid, rsg1, false);
    MultiPathGenerator<rsg_type> generator2(process2, grid, rsg2, false);

    const Size nrTrails = 250000;
    std::vector<GeneralStatistics> stat1(process1->size());
    std::vector<GeneralStatistics> stat2(process2->size());
    std::vector<GeneralStatistics> stat3(process2->size()-1);
    for (i=0; i<nrTrails; ++i) {
        sample_type path1 = generator1.next();
        sample_type path2 = generator2.next();

        std::vector<Rate> rates1(len);
        std::vector<Rate> rates2(len);
        for (Size j=0; j<process1->size(); ++j) {
            rates1[j] = path1.value[j][location[j]];
            rates2[j] = path2.value[j][location[j]];
        }

        std::vector<DiscountFactor> dis1 = process1->discountBond(rates1);
        std::vector<DiscountFactor> dis2 = process2->discountBond(rates2);

        for (Size k=0; k<process1->size(); ++k) {
            Real accrualPeriod =  process1->accrualEndTimes()[k]
                                - process1->accrualStartTimes()[k];
            // caplet payoff function, cap rate at 4%
            Real payoff1 = std::max(rates1[k] - 0.04, 0.0) * accrualPeriod;

            Real payoff2 = std::max(rates2[k] - 0.04, 0.0) * accrualPeriod;
            stat1[k].add(dis1[k] * payoff1);
            stat2[k].add(dis2[k] * payoff2);

            if (k != 0) {
                // ratchet cap payoff function
                Real payoff3 =  std::max(rates2[k] - (rates2[k-1]+0.0025), 0.0)
                              * accrualPeriod;
                stat3[k-1].add(dis2[k] * payoff3);
            }
        }

    }

    Real capletNpv[] = {0.000000000000, 0.000002841629, 0.002533279333,
                        0.009577143571, 0.017746502618, 0.025216116835,
                        0.031608230268, 0.036645683881, 0.039792254012,
                        0.041829864365};

    Real ratchetNpv[] = {0.0082644895, 0.0082754754, 0.0082159966,
                         0.0082982822, 0.0083803357, 0.0084366961,
                         0.0084173270, 0.0081803406, 0.0079533814};

    for (Size k=0; k < process1->size(); ++k) {

        Real calculated1 = stat1[k].mean();
        Real tolerance1  = stat1[k].errorEstimate();
        Real expected    = capletNpv[k];

        if (std::fabs(calculated1 - expected) > tolerance1) {
            BOOST_ERROR("Failed to reproduce expected caplet NPV"
                        << "\n    calculated: " << calculated1
                        << "\n    error int:  " << tolerance1
                        << "\n    expected:   " << expected);
        }

        Real calculated2 = stat2[k].mean();
        Real tolerance2  = stat2[k].errorEstimate();

        if (std::fabs(calculated2 - expected) > tolerance2) {
            BOOST_ERROR("Failed to reproduce expected caplet NPV"
                        << "\n    calculated: " << calculated2
                        << "\n    error int:  " << tolerance2
                        << "\n    expected:   " << expected);
        }

        if (k != 0) {
            Real calculated3 = stat3[k-1].mean();
            Real tolerance3  = stat3[k-1].errorEstimate();
            expected    = ratchetNpv[k-1];

            Real refError = 1e-5; // 1e-5. error bars of the reference values

            if (std::fabs(calculated3 - expected) > tolerance3 + refError) {
                BOOST_ERROR("Failed to reproduce expected caplet NPV"
                            << "\n    calculated: " << calculated3
                            << "\n    error int:  " << tolerance3 + refError
                            << "\n    expected:   " << expected);
            }
        }
    }

    QL_TEST_TEARDOWN
}

test_suite* LiborMarketModelProcessTest::suite() {
    test_suite* suite = BOOST_TEST_SUITE("Libor market model process tests");

    suite->add(BOOST_TEST_CASE(
         &LiborMarketModelProcessTest::testInitialisation));
    suite->add(BOOST_TEST_CASE(
         &LiborMarketModelProcessTest::testLambdaBootstrapping));
    suite->add(BOOST_TEST_CASE(
          &LiborMarketModelProcessTest::testMonteCarloCapletPricing));
    return suite;
}


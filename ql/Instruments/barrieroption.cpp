
/*
 Copyright (C) 2003 Neil Firth
 Copyright (C) 2003 Ferdinando Ametrano
 Copyright (C) 2000, 2001, 2002, 2003 RiskMap srl

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it under the
 terms of the QuantLib license.  You should have received a copy of the
 license along with this program; if not, please email quantlib-dev@lists.sf.net
 The license is also available online at http://quantlib.org/html/license.html

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

/*! \file barrieroption.cpp
    \brief Barrier option on a single asset
*/

#include <ql/Volatilities/blackconstantvol.hpp>
#include <ql/Instruments/barrieroption.hpp>
#include <ql/PricingEngines/Barrier/barrierengines.hpp>

namespace QuantLib {

    BarrierOption::BarrierOption(
                         Barrier::Type barrierType,
                         double barrier,
                         double rebate,
                         Option::Type type,
                         const RelinkableHandle<Quote>& underlying,
                         double strike,
                         const RelinkableHandle<TermStructure>& dividendTS,
                         const RelinkableHandle<TermStructure>& riskFreeTS,
                         const Exercise& exercise,
                         const RelinkableHandle<BlackVolTermStructure>& volTS,
                         const Handle<PricingEngine>& engine,
                         const std::string& isinCode, 
                         const std::string& description)
    : Option(engine, isinCode, description),
      barrierType_(barrierType), barrier_(barrier), rebate_(rebate),
      payoff_(new PlainVanillaPayoff(type,strike)), underlying_(underlying),
      exercise_(exercise), 
      riskFreeTS_(riskFreeTS), dividendTS_(dividendTS),
      volTS_(volTS) {

        if (IsNull(engine))
            setPricingEngine(Handle<PricingEngine>(new AnalyticBarrierEngine));

        registerWith(underlying_);
        registerWith(dividendTS_);
        registerWith(riskFreeTS_);
        registerWith(volTS_);
    }

    double BarrierOption::delta() const {
        calculate();
        QL_REQUIRE(delta_ != Null<double>(),
                   "BarrierOption: delta not provided");
        return delta_;
    }

    double BarrierOption::gamma() const {
        calculate();
        QL_REQUIRE(gamma_ != Null<double>(),
                   "BarrierOption: gamma not provided");
        return gamma_;
    }

    double BarrierOption::theta() const {
        calculate();
        QL_REQUIRE(theta_ != Null<double>(),
                   "BarrierOption: theta not provided");
        return theta_;
    }

    double BarrierOption::vega() const {
        calculate();
        QL_REQUIRE(vega_ != Null<double>(),
                   "BarrierOption: vega not provided");
        return vega_;
    }

    double BarrierOption::rho() const {
        calculate();
        QL_REQUIRE(rho_ != Null<double>(),
                   "BarrierOption: rho not provided");
        return rho_;
    }

    double BarrierOption::dividendRho() const {
        calculate();
        QL_REQUIRE(dividendRho_ != Null<double>(),
                   "BarrierOption: dividend rho not provided");
        return dividendRho_;
    }

    double BarrierOption::strikeSensitivity() const {
        calculate();
        QL_REQUIRE(strikeSensitivity_ != Null<double>(),
                   "BarrierOption: strike sensitivity not provided");
        return strikeSensitivity_;
    }

    bool BarrierOption::isExpired() const {
        return exercise_.lastDate() < riskFreeTS_->referenceDate();
    }

    void BarrierOption::setupExpired() const {
        NPV_ = delta_ = gamma_ = theta_ =
            vega_ = rho_ = dividendRho_ = strikeSensitivity_ = 0.0;
    }

    void BarrierOption::setupArguments(Arguments* args) const {
        BarrierOption::arguments* arguments =
            dynamic_cast<BarrierOption::arguments*>(args);
        QL_REQUIRE(arguments != 0,
                   "BarrierOption::setupArguments : "
                   "wrong argument type");

        arguments->payoff = payoff_;

        arguments->barrierType = barrierType_;
        arguments->barrier = barrier_;
        arguments->rebate = rebate_;

        QL_REQUIRE(!IsNull(underlying_),
                   "BarrierOption::setupArguments : "
                   "null underlying price given");
        arguments->underlying = underlying_->value();

        // should I require !IsNull(TS) ???
        arguments->dividendTS = dividendTS_;
        arguments->riskFreeTS = riskFreeTS_;

        arguments->maturity = riskFreeTS_->dayCounter().yearFraction(
                          riskFreeTS_->referenceDate(), exercise_.lastDate());
        arguments->exerciseType = exercise_.type();
        arguments->stoppingTimes = 
            std::vector<Time>(exercise_.dates().size());
        for (Size i=0; i<exercise_.dates().size(); i++) {
            arguments->stoppingTimes[i] = 
                riskFreeTS_->dayCounter().yearFraction(
                             riskFreeTS_->referenceDate(), exercise_.date(i));
        }

        arguments->volTS = volTS_;
    }

    void BarrierOption::performCalculations() const {
        Option::performCalculations();
        const Greeks* results =
            dynamic_cast<const Greeks*>(engine_->results());
        QL_ENSURE(results != 0,
                  "BarrierOption::performCalculations : "
                  "no greeks returned from pricing engine");
        /* no check on null values - just copy.
           this allows:
           a) to decide in derived options what to do when null 
           results are returned (throw? numerical calculation?)
           b) to implement slim engines which only calculate the
           value---of course care must be taken not to call
           the greeks methods when using these.
        */
        delta_       = results->delta;
        gamma_       = results->gamma;
        theta_       = results->theta;
        vega_        = results->vega;
        rho_         = results->rho;
        dividendRho_ = results->dividendRho;
        strikeSensitivity_ = results->strikeSensitivity;

        QL_ENSURE(NPV_ != Null<double>(),
                  "BarrierOption::performCalculations : "
                  "null value returned from option pricer");
    }

    void BarrierOption::arguments::validate() const {
        #if defined(QL_PATCH_MICROSOFT)
        VanillaOption::arguments copy = *this;
        copy.validate();
        #else
        VanillaOption::arguments::validate();
        #endif

        switch (barrierType) {
          case Barrier::DownIn:
            QL_REQUIRE(underlying >= barrier, "underlying (" +
                       DoubleFormatter::toString(underlying) +
                       ") < barrier (" +
                       DoubleFormatter::toString(barrier) +
                       "): down-and-in barrier undefined");
            break;
          case Barrier::UpIn:
            QL_REQUIRE(underlying <= barrier, "underlying ("+
                       DoubleFormatter::toString(underlying) +
                       ") > barrier ("+
                       DoubleFormatter::toString(barrier) +
                       "): up-and-in barrier undefined");
            break;
          case Barrier::DownOut:
            QL_REQUIRE(underlying >= barrier, "underlying ("+
                       DoubleFormatter::toString(underlying) +
                       ") < barrier ("+ 
                       DoubleFormatter::toString(barrier) +
                       "): down-and-out barrier undefined");
            break;
          case Barrier::UpOut:
            QL_REQUIRE(underlying <= barrier, "underlying ("+
                       DoubleFormatter::toString(underlying) +
                       ") > barrier ("+
                       DoubleFormatter::toString(barrier) +
                       "): up-and-out barrier undefined");
            break;
          default:
            throw Error("Barrier Option: unknown type");
        }
    }

}


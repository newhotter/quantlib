
/*
 * Copyright (C) 2000-2001 QuantLib Group
 *
 * This file is part of QuantLib.
 * QuantLib is a C++ open source library for financial quantitative
 * analysts and developers --- http://quantlib.sourceforge.net/
 *
 * QuantLib is free software and you are allowed to use, copy, modify, merge,
 * publish, distribute, and/or sell copies of it under the conditions stated
 * in the QuantLib License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the license for more details.
 *
 * You should have received a copy of the license along with this file;
 * if not, contact ferdinando@ametrano.net
 * The license is also available at http://quantlib.sourceforge.net/LICENSE.TXT
 *
 * The members of the QuantLib Group are listed in the Authors.txt file, also
 * available at http://quantlib.sourceforge.net/Authors.txt
*/

/*! \file dividendoption.cpp
    \brief base class for options with dividends

    $Source$
    $Log$
    Revision 1.12  2001/05/22 13:30:37  marmar
    sMin_ and sMax_ are computed in a different way now

    Revision 1.11  2001/04/26 16:05:42  marmar
    underlying_ not mutable anymore, setGridLimits accepts the value for center

    Revision 1.10  2001/04/23 14:23:18  marmar
    Tricky bug fixed: american condition was applied to control price as well

    Revision 1.9  2001/04/23 07:48:21  marmar
    Control variate changed back, error messages modified

    Revision 1.8  2001/04/23 07:34:23  marmar
    Changed control variate

    Revision 1.7  2001/04/09 14:13:34  nando
    all the *.hpp moved below the Include/ql level

    Revision 1.6  2001/04/06 18:46:22  nando
    changed Authors, Contributors, Licence and copyright header

    Revision 1.5  2001/04/06 16:12:18  marmar
    Bug fixed in multi-period option

    Revision 1.4  2001/04/04 12:13:24  nando
    Headers policy part 2:
    The Include directory is added to the compiler's include search path.
    Then both your code and user code specifies the sub-directory in
    #include directives, as in
    #include <Solvers1d/newton.hpp>

    Revision 1.3  2001/04/04 11:07:24  nando
    Headers policy part 1:
    Headers should have a .hpp (lowercase) filename extension
    All *.h renamed to *.hpp

    Revision 1.2  2001/03/21 10:48:08  marmar
    valueAtCenter, firstDerivativeAtCenter, secondDerivativeAtCenter,
    are no longer methods of BSMNumericalOption but separate
    functions

    Revision 1.1  2001/03/20 15:27:38  marmar
    DividendOption and DividendShoutOption are examples of
     MultiPeriodOption's

*/

#include "ql/Pricers/dividendoption.hpp"
#include "ql/Math/cubicspline.hpp"
#include "ql/Pricers/dividendeuropeanoption.hpp"
#include "ql/FiniteDifferences/valueatcenter.hpp"
#include "ql/dataformatters.hpp"
#include <vector>

namespace QuantLib {

    namespace Pricers {

        using Math::CubicSpline;
        using FiniteDifferences::valueAtCenter;

        DividendOption::DividendOption(Type type, double underlying,
            double strike, Rate dividendYield, Rate riskFreeRate,
            Time residualTime, double volatility,
            const std::vector<double>& dividends,
            const std::vector<Time>& exdivdates, int timeSteps, int gridPoints)
        : dividends_(dividends),
          MultiPeriodOption(type, underlying - addElements(dividends),
          strike, dividendYield, riskFreeRate, residualTime, volatility,
          exdivdates, timeSteps, gridPoints) {

            QL_REQUIRE(dateNumber_ == dividends_.size(),
                       "the number of dividends(" +
                       IntegerFormatter::toString(dividends_.size()) +
                       ") is different from the number of dates(" +
                       IntegerFormatter::toString(dateNumber_) +
                       ")");

            QL_REQUIRE(underlying > addElements(dividends),
                       "Dividends(" +
                       DoubleFormatter::toString(underlying - underlying_) +
                       ") cannot exceed underlying(" +
                       DoubleFormatter::toString(underlying) +
                       ")");
        }

        void DividendOption::initializeControlVariate() const{
            analytic_ = Handle<BSMOption> (new 
                            DividendEuropeanOption (type_,
                                underlying_ + addElements(dividends_), 
                                strike_,
                                dividendYield_, 
                                riskFreeRate_, 
                                residualTime_,
                                volatility_, 
                                dividends_, 
                                dates_));
        }

        void DividendOption::executeIntermediateStep(int step) const{

            double newSMin = sMin_ + dividends_[step];
            setGridLimits(center_ + dividends_[step], dates_[step]);
            if (newSMin > sMin_){
                sMin_ = newSMin;
                sMax_ = center_/(sMin_/center_);
            }
            Array oldGrid = grid_ + dividends_[step];
            
            initializeGrid();    
            initializeInitialCondition();
            // This operation was faster than the obvious:
            //     movePricesBeforeExDiv(initialPrices_, grid_, oldGrid);

            movePricesBeforeExDiv(prices_,        grid_, oldGrid);
            movePricesBeforeExDiv(controlPrices_, grid_, oldGrid);
            initializeOperator();
            initializeModel();
            initializeStepCondition();
            stepCondition_ -> applyTo(prices_, dates_[step]);
        }

        void DividendOption::movePricesBeforeExDiv(
                Array& prices, const Array& newGrid, 
                               const Array& oldGrid) const {

            int j, gridSize = oldGrid.size();

            std::vector<double> logOldGrid(0);
            std::vector<double> tmpPrices(0);

            for(j = 0; j<gridSize; j++){
                double p = prices[j];
                double g = oldGrid[j];
                if (g > 0){
                    logOldGrid.push_back(QL_LOG(g));
                    tmpPrices.push_back(p);
                }
            }

            CubicSpline<Array::iterator, Array::iterator> priceSpline(
                logOldGrid.begin(), logOldGrid.end(), tmpPrices.begin());

            int jGrid;
            for (j = 0; j < gridSize; j++){
                if (newGrid[j] >= oldGrid[gridSize-2] )
                    jGrid = gridSize-2;
                else 
                    jGrid = j;
                double logNewGridPoint = QL_LOG(newGrid[jGrid]);                
                prices[j] = priceSpline(logNewGridPoint);
            }

        }

    }

}

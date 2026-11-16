// Copyright 2016 Ismael Jimenez Martinez. All rights reserved.
// Copyright 2017 Roman Lebedev. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "statistics.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>

#include "benchmark/reporter.h"
#include "benchmark/statistics.h"
#include "benchmark/types.h"
#include "check.h"

namespace benchmark {

const auto StatisticsSum = [](const std::vector<double>& v) {
  return std::accumulate(v.begin(), v.end(), 0.0);
};

double StatisticsMean(const std::vector<double>& v) {
  if (v.empty()) {
    return 0.0;
  }
  return StatisticsSum(v) * (1.0 / static_cast<double>(v.size()));
}

double StatisticsMedian(const std::vector<double>& v) {
  if (v.size() < 3) {
    return StatisticsMean(v);
  }
  std::vector<double> copy(v);

  auto center = copy.begin() + v.size() / 2;
  std::nth_element(copy.begin(), center, copy.end());

  // Did we have an odd number of samples?  If yes, then center is the median.
  // If not, then we are looking for the average between center and the value
  // before.  Instead of resorting, we just look for the max value before it,
  // which is not necessarily the element immediately preceding `center` Since
  // `copy` is only partially sorted by `nth_element`.
  if (v.size() % 2 == 1) {
    return *center;
  }
  auto center2 = std::max_element(copy.begin(), center);
  return (*center + *center2) / 2.0;
}

// Return the sum of the squares of this sample set
const auto SumSquares = [](const std::vector<double>& v) {
  return std::inner_product(v.begin(), v.end(), v.begin(), 0.0);
};

const auto Sqr = [](const double dat) { return dat * dat; };
const auto Sqrt = [](const double dat) {
  // Avoid NaN due to imprecision in the calculations
  if (dat < 0.0) {
    return 0.0;
  }
  return std::sqrt(dat);
};

double StatisticsStdDev(const std::vector<double>& v) {
  const auto mean = StatisticsMean(v);
  if (v.empty()) {
    return mean;
  }

  // Sample standard deviation is undefined for n = 1
  if (v.size() == 1) {
    return 0.0;
  }

  const double avg_squares =
      SumSquares(v) * (1.0 / static_cast<double>(v.size()));
  return Sqrt(static_cast<double>(v.size()) /
              (static_cast<double>(v.size()) - 1.0) *
              (avg_squares - Sqr(mean)));
}

double StatisticsCV(const std::vector<double>& v) {
  if (v.size() < 2) {
    return 0.0;
  }

  const auto stddev = StatisticsStdDev(v);
  const auto mean = StatisticsMean(v);

  if (std::fpclassify(mean) == FP_ZERO) {
    return 0.0;
  }

  return stddev / mean;
}

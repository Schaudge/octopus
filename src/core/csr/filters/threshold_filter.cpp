// Copyright (c) 2015-2021 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "threshold_filter.hpp"

#include <utility>
#include <numeric>
#include <functional>
#include <iterator>
#include <algorithm>

#include "io/variant/vcf_header.hpp"
#include "utils/append.hpp"
#include "utils/concat.hpp"
#include "config/octopus_vcf.hpp"

namespace octopus { namespace csr {

namespace {

auto extract_measures(std::vector<ThresholdVariantCallFilter::Condition>& conditions)
{
    std::vector<MeasureWrapper> result {};
    result.reserve(conditions.size());
    for (auto& condition : conditions) {
        result.push_back(std::move(condition.measure));
    }
    return result;
}

auto extract_measures(std::vector<ThresholdVariantCallFilter::Condition>& first,
                      std::vector<ThresholdVariantCallFilter::Condition>& second)
{
    auto result = extract_measures(first);
    utils::append(extract_measures(second), result);
    return result;
}

auto extract_thresholds(std::vector<ThresholdVariantCallFilter::Condition>& conditions)
{
    std::vector<ThresholdVariantCallFilter::ThresholdWrapper> result {};
    result.reserve(conditions.size());
    for (auto& condition : conditions) {
        result.push_back(std::move(condition.threshold));
    }
    return result;
}

auto extract_vcf_filter_keys(std::vector<ThresholdVariantCallFilter::Condition>& conditions)
{
    std::vector<std::string> result {};
    result.reserve(conditions.size());
    for (auto& condition : conditions) {
        result.push_back(std::move(condition.vcf_filter_key));
    }
    return result;
}

bool are_all_unique(std::vector<std::string> keys)
{
    std::sort(std::begin(keys), std::end(keys));
    return std::adjacent_find(std::cbegin(keys), std::cend(keys)) == std::cend(keys);
}

} // namespace

ThresholdVariantCallFilter::ThresholdVariantCallFilter(FacetFactory facet_factory,
                                                       ConditionVectorPair conditions,
                                                       OutputOptions output_config,
                                                       ConcurrencyPolicy threading,
                                                       boost::optional<ProgressMeter&> progress,
                                                       std::vector<MeasureWrapper> other_measures)
: SinglePassVariantCallFilter {std::move(facet_factory),
                               concat(extract_measures(conditions.hard, conditions.soft), std::move(other_measures)),
                               output_config, threading, progress}
, hard_thresholds_ {extract_thresholds(conditions.hard)}
, soft_thresholds_ {extract_thresholds(conditions.soft)}
, vcf_filter_keys_ {extract_vcf_filter_keys(conditions.soft)}
, all_unique_filter_keys_ {are_all_unique(vcf_filter_keys_)}
{}

std::string ThresholdVariantCallFilter::do_name() const
{
    return "threshold";
}

bool ThresholdVariantCallFilter::passes_all_filters(MeasureIterator first_measure, MeasureIterator last_measure,
                                                    ThresholdIterator first_threshold) const
{
    return std::inner_product(first_measure, last_measure, first_threshold, true, std::multiplies<> {},
                              [] (const auto& measure, const auto& threshold) -> bool { return threshold(measure); });
}

void ThresholdVariantCallFilter::annotate(VcfHeader::Builder& header) const
{
    for (const auto& key : vcf_filter_keys_) {
        octopus::vcf::add_filter(header, key);
    }
}

VariantCallFilter::Classification ThresholdVariantCallFilter::classify(const MeasureVector& measures) const
{
    if (passes_all_hard_filters(measures)) {
        if (passes_all_soft_filters(measures)) {
            return Classification {Classification::Category::unfiltered};
        } else {
            return Classification {Classification::Category::soft_filtered, get_failing_vcf_filter_keys(measures)};
        }
    } else {
        return Classification {Classification::Category::hard_filtered};
    }
}

bool ThresholdVariantCallFilter::passes_all_hard_filters(const MeasureVector& measures) const
{
    const auto last_hard = std::next(std::cbegin(measures), hard_thresholds_.size());
    return passes_all_filters(std::cbegin(measures), last_hard, std::cbegin(hard_thresholds_));
}

bool ThresholdVariantCallFilter::passes_all_soft_filters(const MeasureVector& measures) const
{
    const auto first_soft = std::next(std::cbegin(measures), hard_thresholds_.size());
    const auto last_soft = std::next(first_soft, soft_thresholds_.size());
    return passes_all_filters(first_soft, last_soft, std::cbegin(soft_thresholds_));
}

std::vector<std::string> ThresholdVariantCallFilter::get_failing_vcf_filter_keys(const MeasureVector& measures) const
{
    std::vector<std::string> result {};
    result.reserve(soft_thresholds_.size());
    for (std::size_t i {0}; i < soft_thresholds_.size(); ++i) {
        if (!soft_thresholds_[i](measures[i + hard_thresholds_.size()])) {
            result.push_back(vcf_filter_keys_[i]);
        }
    }
    if (!all_unique_filter_keys_) {
        std::sort(std::begin(result), std::end(result));
        result.erase(std::unique(std::begin(result), std::end(result)), std::end(result));
    }
    return result;
}

} // namespace csr
} // namespace octopus

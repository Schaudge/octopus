// Copyright (c) 2015-2020 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef read_algorithms_hpp
#define read_algorithms_hpp

#include <unordered_map>
#include <vector>
#include <iterator>

#include "basics/genomic_region.hpp"
#include "basics/aligned_read.hpp"
#include "containers/mappable_flat_multi_set.hpp"
#include "mappable_algorithms.hpp"

namespace octopus {

namespace detail {

template <typename T, typename F>
std::unordered_map<AlignedRead, unsigned>
coverages_in_read_regions(const T& reads, const GenomicRegion& region, F f)
{
    std::unordered_map<AlignedRead, unsigned> result {};
    result.reserve(reads.size());
    const auto position_coverages = calculate_positional_coverage(reads, region);
    const auto first_position = mapped_begin(region);
    const auto num_positions  = region_size(region);
    for (const auto& read : reads) {
        auto first = std::next(std::cbegin(position_coverages), (mapped_begin(read) <= first_position) ? 0 : mapped_begin(read) - first_position);
        auto last  = std::next(std::cbegin(position_coverages), std::min(mapped_end(read) - first_position, num_positions));
        result.emplace(read, *f(first, last));
    }
    return result;
}

} // namespace detail

template <typename T>
std::unordered_map<AlignedRead, unsigned>
get_min_coverages_in_read_regions(const T& reads, const GenomicRegion& region)
{
    return detail::coverages_in_read_regions(reads, region, std::min_element);
}

template <typename T>
std::unordered_map<AlignedRead, unsigned>
get_max_coverages_in_read_regions(const T& reads, const GenomicRegion& region)
{
    return detail::coverages_in_read_regions(reads, region, std::max_element);
}

template <typename Range,
          typename = std::enable_if_t<std::is_same<AlignedRead, typename Range::value_type>::value>>
std::vector<GenomicRegion>
find_high_coverage_regions(const Range& reads, const GenomicRegion& region, const unsigned max_coverage)
{
    auto depths = calculate_positional_coverage(reads, region);
    return find_high_coverage_regions(depths, region, max_coverage);
}

template <typename Range,
          typename = std::enable_if_t<std::is_same<AlignedRead, typename Range::value_type>::value>>
std::vector<GenomicRegion>
find_high_coverage_regions(const Range& reads, const unsigned max_coverage)
{
    return find_high_coverage_regions(reads, encompassing_region(reads), max_coverage);
}

template <typename ReadMap>
std::unordered_map<typename ReadMap::key_type, std::vector<GenomicRegion>>
find_high_coverage_regions(const ReadMap& reads, const GenomicRegion& region, const unsigned max_coverage)
{
    std::unordered_map<typename ReadMap::key_type, std::vector<GenomicRegion>> result {};
    result.reserve(reads.size());
    for (const auto& sample_reads : reads) {
        result.emplace(sample_reads.first, find_high_coverage_regions(sample_reads.second, region, max_coverage));
    }
    return result;
}

template <typename T>
std::vector<GenomicRegion> find_uniform_coverage_regions(const T& reads, const GenomicRegion& region)
{
    const auto coverages = calculate_positional_coverage(reads, region);
    std::vector<GenomicRegion> result {};
    result.reserve(region_size(region));
    if (coverages.empty()) return result;
    const auto contig = contig_name(region);
    auto begin = mapped_begin(region);
    auto end   = begin;
    auto previous_coverage = coverages.front();
    for (const auto coverage : coverages) {
        if (coverage != previous_coverage) {
            result.emplace_back(contig, begin, end);
            begin = end;
            previous_coverage = coverage;
        }
        ++end;
    }
    result.emplace_back(contig, begin, end);
    return result;
}

template <typename T>
std::vector<GenomicRegion> find_uniform_coverage_regions(const T& reads)
{
    return find_uniform_coverage_regions(reads, encompassing_region(reads));
}

namespace detail {

inline MappableFlatMultiSet<AlignedRead>
copy_each(const MappableFlatMultiSet<AlignedRead>& reads, const GenomicRegion& region, NonMapTag)
{
    MappableFlatMultiSet<AlignedRead> result {};
    result.reserve(reads.size());
    for (const auto& read : reads) {
        result.emplace(copy(read, region));
    }
    return result;
}

template <typename T>
T copy_each(const T& reads, const GenomicRegion& region, NonMapTag)
{
    T result {};
    result.reserve(reads.size());
    std::transform(std::cbegin(reads), std::cend(reads), std::back_inserter(result),
                   [&region] (const auto& read) { return copy(read, region); });
    return result;
}

template <typename T>
T copy_each(const T& reads, const GenomicRegion& region, MapTag)
{
    T result {};
    result.reserve(reads.size());
    for (const auto& p : reads) {
        result.emplace(p.first, copy_each(p.second, region, NonMapTag {}));
    }
    return result;
}

} // namespace detail

template <typename T>
T copy_each(const T& reads, const GenomicRegion& region)
{
    return detail::copy_each(reads, region, MapTagType<T> {});
}

} // namespace octopus

#endif

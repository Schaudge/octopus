// Copyright (c) 2015-2019 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "downsampler.hpp"

#include <vector>
#include <deque>
#include <functional>
#include <iterator>
#include <algorithm>
#include <numeric>
#include <random>
#include <cassert>

#include <boost/random/uniform_int_distribution.hpp>
#include <boost/random/discrete_distribution.hpp>

#include "basics/mappable_reference_wrapper.hpp"
#include "concepts/mappable_range.hpp"
#include "utils/mappable_algorithms.hpp"
#include "utils/read_algorithms.hpp"
#include "utils/append.hpp"
#include "utils/random_select.hpp"

// Use boost distributions as std distributions not guaranteed to be deterministic across compilers.
// Generators are however.
// https://stackoverflow.com/questions/48730363/if-we-seed-c11-mt19937-as-the-same-on-different-machines-will-we-get-the-same.

namespace octopus { namespace readpipe {

Downsampler::Report::DownsampleRegion::DownsampleRegion(GenomicRegion region, std::size_t num_reads)
: region_ {std::move(region)}
, num_reads_ {num_reads}
{}

namespace {

using ReadWrapper = MappableReferenceWrapper<const AlignedRead>;

using PositionCoverages = std::vector<unsigned>;

bool has_minimum_coverage(const PositionCoverages& required_coverage)
{
    return std::all_of(std::cbegin(required_coverage), std::cend(required_coverage),
                       [] (const auto coverage) noexcept { return coverage == 0; });
}

template <typename ForwardIt>
auto calculate_minimum_coverages(const ForwardIt first, const ForwardIt last,
                                 const GenomicRegion& region, const unsigned min_coverage)
{
    auto result = calculate_positional_coverage(first, last, region);
    std::transform(std::cbegin(result), std::cend(result), std::begin(result),
                   [min_coverage] (const auto coverage) noexcept {
                       return std::min(coverage, min_coverage);
                   });
    return result;
}

template <typename RandomGenerator>
auto sample(const PositionCoverages& required_coverage, RandomGenerator& generator)
{
    // TODO: Do we really need to keep regenerating this distribution?
    boost::random::discrete_distribution<std::size_t> dist {std::cbegin(required_coverage), std::cend(required_coverage)};
    return dist(generator);
}

template <typename T, typename RandomGenerator>
auto random_sample(const OverlapRange<T>& range, RandomGenerator& generator)
{
    return random_select(std::begin(range), std::end(range), generator).base();
}

template <typename BidirIt, typename RandomGenerator>
auto pick_sample(BidirIt first_unsampled, BidirIt last_unsampled,
                 const std::vector<GenomicRegion>& positions,
                 const PositionCoverages& required_coverage,
                 const AlignedRead::MappingDomain::Size max_read_size,
                 RandomGenerator& generator)
{
    assert(first_unsampled < last_unsampled);
    const auto candidates = overlap_range(first_unsampled, last_unsampled,
                                          positions[sample(required_coverage, generator)],
                                          max_read_size);
    assert(!candidates.empty());
    return random_sample(candidates, generator);
}

void reduce(PositionCoverages& coverages, const ReadWrapper& read, const GenomicRegion& region)
{
    assert(!begins_before(read, region));
    const auto read_offset = begin_distance(region, read);
    const auto first = std::next(std::begin(coverages), read_offset);
    auto last = std::next(first, region_size(read));
    if (last > std::end(coverages)) last = std::end(coverages);
    std::transform(first, last, first, [] (const auto x) noexcept { return (x > 0) ? x - 1 : 0; });
}

template <typename ForwardIt>
ForwardIt shift_left(ForwardIt first, ForwardIt last)
{
    return std::rotate(first, std::next(first), last);
}

template <typename BidirIt>
BidirIt shift_right(BidirIt first, BidirIt last)
{
    const auto rfirst = std::make_reverse_iterator(last);
    const auto rlast  = std::make_reverse_iterator(first);
    return std::rotate(rfirst, std::next(rfirst), rlast).base();
}

template <typename BidirIt>
void remove_sample(BidirIt& first, BidirIt sample, BidirIt& last)
{
    assert(first <= sample && sample < last);
    // rotating is linear time so it's cheaper to shift the sample into the closer of the
    // two sampled groups.
    if (std::distance(first, sample) < std::distance(sample, last)) {
        if (sample != first) {
            shift_right(first, std::next(sample));
        }
        ++first;
    } else {
        if (sample != std::prev(last)) {
            shift_left(sample, last);
        }
        --last;
    }
}

auto extract_sampled(std::vector<ReadWrapper>& reads,
                     std::vector<ReadWrapper>::iterator first_unsampled,
                     std::vector<ReadWrapper>::iterator last_unsampled)
{
    reads.erase(first_unsampled, last_unsampled);
    std::sort(std::begin(reads), std::end(reads));
    std::vector<AlignedRead> result {};
    result.reserve(reads.size());
    std::transform(std::begin(reads), std::end(reads), std::back_inserter(result),
                   [] (const auto& wrapper) -> AlignedRead { return wrapper.get(); });
    return result;
}

} // namespace

template <typename InputIt, typename RandomGenerator>
auto sample(const InputIt first_read, const InputIt last_read, const GenomicRegion& region,
            const unsigned target_coverage, RandomGenerator& generator)
{
    if (first_read == last_read) return std::vector<AlignedRead> {};
    const auto positions = decompose(region);
    auto required_coverage = calculate_minimum_coverages(first_read, last_read, region, target_coverage);
    assert(positions.size() == required_coverage.size());
    std::vector<ReadWrapper> reads {first_read, last_read};
    const auto max_read_size = size(largest_region(reads)); // for efficient overlap detection
    // The reads are partitioned into three groups: sampled | unsampled | sampled
    // which allows a sampled read to be optimally moved out of the unsampled partition
    auto first_unsampled_itr = std::begin(reads);
    auto last_unsampled_itr  = std::end(reads);
    while (!has_minimum_coverage(required_coverage)) {
        const auto sampled_itr = pick_sample(first_unsampled_itr, last_unsampled_itr,
                                             positions, required_coverage, max_read_size,
                                             generator);
        reduce(required_coverage, *sampled_itr, region);
        remove_sample(first_unsampled_itr, sampled_itr, last_unsampled_itr);
    }
    return extract_sampled(reads, first_unsampled_itr, last_unsampled_itr);
}

namespace {

// Look for regions with coverage above max_coverage interconnected by positions
// with coverage abouve min_coverage. The idea being samples are better taken from the
// larger joined regions.
auto find_target_regions(const ReadContainer& reads, const unsigned max_coverage, const unsigned min_coverage)
{
    const auto above_max_coverage_regions = find_high_coverage_regions(reads, max_coverage);
    std::vector<GenomicRegion> result {};
    if (above_max_coverage_regions.empty()) return result;
    result.reserve(above_max_coverage_regions.size());
    auto above_min_coverage_regions = find_high_coverage_regions(reads, min_coverage);
    std::copy_if(std::make_move_iterator(std::begin(above_min_coverage_regions)),
                 std::make_move_iterator(std::end(above_min_coverage_regions)),
                 std::back_inserter(result),
                 [&above_max_coverage_regions] (const auto& region) {
                     return has_contained(std::cbegin(above_max_coverage_regions), std::cend(above_max_coverage_regions), region);
                 });
    return result;
}

} // namespace

Downsampler::Report sample(ReadContainer& reads, const unsigned trigger_coverage, const unsigned target_coverage)
{
    using std::begin; using std::end; using std::make_move_iterator;
    
    Downsampler::Report result {};
    
    if (reads.empty()) return result;
    const auto targets = find_target_regions(reads, trigger_coverage, target_coverage);
    if (targets.empty()) return result;
    
    // We avoid using ReadContainers member methods for inserting sampled reads as they are order
    //  N log(size() + N) + N * size(). This is because they assume the input range is unsorted,
    // and must therefore to a binary search for every read inserted. We can avoid this as
    // we know the reads are originally sorted, and we sample reads in non-overlapping blocks.
    // The blocks are themselves sorted, so no comparisons are required.
    std::vector<std::deque<AlignedRead>> sampled_read_blocks {};
    sampled_read_blocks.reserve(targets.size());
    std::vector<std::vector<AlignedRead>> unsampled_read_blocks {};
    unsampled_read_blocks.reserve(targets.size());
    std::size_t num_reads {0};
    std::mt19937 generator {891106};
    
    // Downsample in reverse order because erasing near back of MappableFlatMultiSet is much
    // cheaper than erasing near front.
    std::for_each(std::crbegin(targets), std::crend(targets), [&] (const auto& region) {
        const auto contained = bases(contained_range(begin(reads), end(reads), region));
        num_reads += std::distance(end(contained), end(reads));
        unsampled_read_blocks.emplace_back(make_move_iterator(end(contained)), make_move_iterator(end(reads)));
        auto sampled_reads = sample(begin(contained), end(contained), region, target_coverage, generator);
        num_reads += sampled_reads.size();
        const auto num_reads_in_target = size(contained);
        assert(num_reads_in_target >= sampled_reads.size());
        const auto num_reads_removed = num_reads_in_target - sampled_reads.size();
        result.downsampled_regions.emplace(region, num_reads_removed);
        sampled_read_blocks.emplace_back(make_move_iterator(begin(sampled_reads)), make_move_iterator(end(sampled_reads)));
        reads.erase(begin(contained), end(reads));
        reads.shrink_to_fit();
    });
    
    num_reads += reads.size();
    std::vector<AlignedRead> buffer {};
    buffer.reserve(num_reads);
    buffer.assign(make_move_iterator(begin(reads)), make_move_iterator(end(reads)));
    reads.clear();
    reads.shrink_to_fit();
    for (auto i = static_cast<int>(targets.size()) - 1; i >= 0; --i) {
        utils::append(std::move(sampled_read_blocks[i]), buffer);
        utils::append(std::move(unsampled_read_blocks[i]), buffer);
    }
    reads = ReadContainer {make_move_iterator(begin(buffer)), make_move_iterator(end(buffer))};
    return result;
}

// Downsampler

Downsampler::Downsampler(const unsigned trigger_coverage, const unsigned target_coverage)
: trigger_coverage_ {trigger_coverage}
, target_coverage_ {target_coverage}
{
    if (target_coverage > trigger_coverage) {
        target_coverage_ = trigger_coverage;
    }
}

Downsampler::Report Downsampler::downsample(ReadContainer& reads) const
{
    return sample(reads, trigger_coverage_, target_coverage_);
}

std::size_t count_downsampled_reads(const DownsamplerReportMap& reports)
{
    return std::accumulate(std::cbegin(reports), std::cend(reports), std::size_t {0},
                           [] (auto curr, const auto& p) {
                               return curr + std::accumulate(std::cbegin(p.second.downsampled_regions),
                                                             std::cend(p.second.downsampled_regions), std::size_t {0},
                                                             [] (auto curr, const auto& target) {
                                   return curr + target.num_reads(); });
                           });
}

std::size_t count_downsampled_reads(const DownsamplerReportMap& reports, const GenomicRegion& region)
{
    return std::accumulate(std::cbegin(reports), std::cend(reports), std::size_t {0},
                           [&region] (auto curr, const auto& p) {
                               const auto overlapped = overlap_range(p.second.downsampled_regions, region);
                               return curr + std::accumulate(std::cbegin(overlapped), std::cend(overlapped), std::size_t {0},
                                                             [] (auto curr, const auto& target) {
                                                                 return curr + target.num_reads(); });
                           });
}

} // namespace readpipe
} // namespace octopus

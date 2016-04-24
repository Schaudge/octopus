//
//  variant_caller.cpp
//  Octopus
//
//  Created by Daniel Cooke on 15/09/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#include "variant_caller.hpp"

#include <algorithm>
#include <utility>
#include <tuple>
#include <iterator>
#include <cassert>
#include <iostream>

#include "genomic_region.hpp"
#include "mappable.hpp"
#include "mappable_algorithms.hpp"
#include "mappable_flat_set.hpp"
#include "read_utils.hpp"
#include "haplotype.hpp"
#include "haplotype_generator.hpp"
#include "haplotype_liklihood_model.hpp"
#include "haplotype_filter.hpp"
#include "maths.hpp"
#include "vcf_record.hpp"
#include "vcf_record_factory.hpp"

#include "timers.hpp"

namespace Octopus
{
// public methods

VariantCaller::CallerParameters::CallerParameters(unsigned max_haplotypes,
                                                  RefCallType refcall_type,
                                                  bool call_sites_only,
                                                  bool allow_lagging,
                                                  double min_phase_score)
:
max_haplotypes {max_haplotypes},
refcall_type {refcall_type},
call_sites_only {call_sites_only},
lag_haplotype_generation {allow_lagging},
min_phase_score {min_phase_score}
{}

VariantCaller::VariantCaller(const ReferenceGenome& reference,
                             ReadPipe& read_pipe,
                             CandidateVariantGenerator&& candidate_generator,
                             CallerParameters parameters)
:
reference_ {reference},
read_pipe_ {read_pipe},
samples_ {read_pipe.get_samples()},
debug_log_ {},
refcall_type_ {parameters.refcall_type},
call_sites_only_ {parameters.call_sites_only},
max_haplotypes_ {parameters.max_haplotypes},
min_haplotype_posterior_ {1e-15},
lag_haplotype_generation_ {parameters.lag_haplotype_generation},
min_phase_score_ {parameters.min_phase_score},
candidate_generator_ {std::move(candidate_generator)}
{
    if (DEBUG_MODE) {
        debug_log_ = Logging::DebugLogger {};
    }
}

namespace debug
{
    template <typename S>
    void print_left_aligned_candidates(S&& stream, const std::vector<Variant>& raw_candidates,
                                       const ReferenceGenome& reference);
    void print_left_aligned_candidates(const std::vector<Variant>& raw_candidates,
                                       const ReferenceGenome& reference);
    
    template <typename S>
    void print_final_candidates(S&& stream, const MappableFlatSet<Variant>& candidates,
                                bool number_only = false);
    void print_final_candidates(const MappableFlatSet<Variant>& candidates,
                                bool number_only = false);
    
    template <typename S>
    void print_active_candidates(S&& stream, const MappableFlatSet<Variant>& candidates,
                                 const GenomicRegion& active_region, bool number_only = false);
    void print_active_candidates(const MappableFlatSet<Variant>& candidates,
                                 const GenomicRegion& active_region, bool number_only = false);
    
    template <typename S>
    void print_inactive_flanking_candidates(S&& stream, const MappableFlatSet<Variant>& candidates,
                                            const GenomicRegion& active_region,
                                            const GenomicRegion& haplotype_region,
                                            bool number_only = false);
    void print_inactive_flanking_candidates(const MappableFlatSet<Variant>& candidates,
                                            const GenomicRegion& active_region,
                                            const GenomicRegion& haplotype_region,
                                            bool number_only = false);
    
    enum class Resolution {Sequence, Alleles, VariantAlleles, SequenceAndAlleles, SequenceAndVariantAlleles};
    
    template <typename S>
    void print_haplotypes(S&& stream, const std::vector<Haplotype>& haplotypes,
                          Resolution resolution = Resolution::SequenceAndAlleles);
    void print_haplotypes(const std::vector<Haplotype>& haplotypes,
                          Resolution resolution = Resolution::SequenceAndAlleles);
    
    template <typename S, typename Map>
    void print_haplotype_posteriors(S&& stream, const Map& haplotype_posteriors, std::size_t n = 5);
    template <typename Map>
    void print_haplotype_posteriors(const Map& haplotype_posteriors, std::size_t n = 5);
    
    auto find_read(const std::string& region, const std::string& cigar_str,
                   const ReadContainer& reads);
    
    auto find_read(const SampleIdType& sample, const std::string& region,
                   const std::string& cigar_str, const ReadMap& reads);
    
    auto find_first_read(const std::string& region, const std::string& cigar_str,
                         const ReadMap& reads);
    
    double calculate_likelihood(const Haplotype& haplotype, const AlignedRead& read,
                                const HaplotypeLikelihoodModel::FlankState flank_state);
    
    void run_likelihood_calculation(const std::string& haplotype_str,
                                    const std::string& haplotype_region,
                                    const std::string& active_region,
                                    const std::string& read_region,
                                    const std::string& cigar_str,
                                    const ReadMap& reads,
                                    const MappableFlatSet<Variant>& candidates,
                                    const ReferenceGenome& reference);
}

auto generate_candidates(CandidateVariantGenerator& generator, const GenomicRegion& region,
                         const ReferenceGenome& reference)
{
    auto raw_candidates = generator.generate_candidates(region);
    
    if (DEBUG_MODE) {
        Logging::DebugLogger log {};
        debug::print_left_aligned_candidates(stream(log), raw_candidates, reference);
    }
    
    auto final_candidates = unique_left_align(std::move(raw_candidates), reference);
    
    generator.clear();
    
    return MappableFlatSet<Variant> {
        std::make_move_iterator(std::begin(final_candidates)),
        std::make_move_iterator(std::end(final_candidates))
    };
}

auto copy_overlapped_to_vector(const MappableFlatSet<Variant>& candidates,
                               const GenomicRegion& region)
{
    const auto overlapped = overlap_range(candidates, region);
    return std::vector<Variant> {std::cbegin(overlapped), std::cend(overlapped)};
}

const auto& haplotype_region(const std::vector<Haplotype>& haplotypes)
{
    return mapped_region(haplotypes.front());
}

void remove_passed_candidates(MappableFlatSet<Variant>& candidates,
                              const GenomicRegion& candidate_region,
                              const GenomicRegion& haplotype_region)
{
    if (begins_before(candidate_region, haplotype_region)) {
        const auto passed_region = left_overhang_region(candidate_region, haplotype_region);
        
        if (DEBUG_MODE) {
            Logging::DebugLogger log {};
            stream(log) << "Removing " << count_overlapped(candidates, passed_region)
                        << " passed candidates in region " << passed_region;
        }
        
        candidates.erase_overlapped(passed_region);
    }
}

template <typename Container>
void remove_duplicate_haplotypes(Container& haplotypes)
{
    const auto n = unique_least_complex(haplotypes);
    if (DEBUG_MODE) {
        Logging::DebugLogger log {};
        stream(log) << n << " duplicate haplotypes were removed";
    }
}

auto calculate_flank_regions(const GenomicRegion& haplotype_region,
                             const GenomicRegion& active_region,
                             const MappableFlatSet<Variant>& candidates)
{
    auto lhs_flank = left_overhang_region(haplotype_region, active_region);
    auto rhs_flank = right_overhang_region(haplotype_region, active_region);
    
    const auto active_candidates = overlap_range(candidates, active_region);
    
    assert(!active_candidates.empty());
    
    if (is_empty_region(*leftmost_mappable(active_candidates)) && !is_empty(lhs_flank)) {
        lhs_flank = expand_rhs(lhs_flank, -1); // stops boundry insertions being considerd inactive
    }
    
    const auto lhs_inactive_candidates = overlap_range(candidates, lhs_flank);
    
    if (lhs_inactive_candidates.empty()) {
        lhs_flank = head_region(lhs_flank);
    } else {
        lhs_flank = closed_region(lhs_flank, rightmost_region(lhs_inactive_candidates));
    }
    
    if (is_empty_region(*rightmost_mappable(active_candidates)) && !is_empty(rhs_flank)) {
        rhs_flank = expand_lhs(rhs_flank, -1); // stops boundry insertions being considerd inactive
    }
    
    const auto rhs_inactive_candidates = overlap_range(candidates, rhs_flank);
    
    if (rhs_inactive_candidates.empty()) {
        rhs_flank = tail_region(rhs_flank);
    } else {
        rhs_flank = closed_region(leftmost_region(rhs_inactive_candidates), rhs_flank);
    }
    
    return std::make_pair(std::move(lhs_flank), std::move(rhs_flank));
}

auto calculate_flank_state(const std::vector<Haplotype>& haplotypes,
                           const GenomicRegion& active_region,
                           const MappableFlatSet<Variant>& candidates)
{
    const auto flanks = calculate_flank_regions(haplotype_region(haplotypes), active_region,
                                                candidates);
    
    return HaplotypeLikelihoodModel::FlankState {
        active_region.get_contig_region(),
        flanks.first.get_contig_region(),
        flanks.second.get_contig_region()
    };
}

bool all_empty(const ReadMap& reads)
{
    return std::all_of(std::cbegin(reads), std::cend(reads),
                       [] (const auto& p) { return p.second.empty(); });
}

auto calculate_candidate_region(const GenomicRegion& call_region, const ReadMap& reads,
                                const CandidateVariantGenerator& candidate_generator)
{
    if (!candidate_generator.requires_reads()) return call_region;
    
    if (all_empty(reads)) {
        return call_region;
    }
    
    return encompassing_region(reads);
}

bool has_passed(const GenomicRegion& next_active_region, const GenomicRegion& active_region)
{
    return is_after(next_active_region, active_region) && active_region != next_active_region;
}

// Wrap the pointer so can use mappable algorithms
struct CallWrapper : public Mappable<CallWrapper>
{
    CallWrapper(std::unique_ptr<Call>&& call) : call {std::move(call) } {}
    operator const std::unique_ptr<Call>&() const noexcept { return call; }
    operator std::unique_ptr<Call>&() noexcept { return call; }
    std::unique_ptr<Call>::pointer operator->() const noexcept { return call.get(); };
    std::unique_ptr<Call> call;
    const GenomicRegion& get_region() const noexcept { return call->get_region(); }
};

template <typename T>
auto wrap(std::vector<std::unique_ptr<T>>&& calls)
{
    return std::vector<CallWrapper> {
        std::make_move_iterator(std::begin(calls)),
        std::make_move_iterator(std::end(calls))
    };
}

auto unwrap(std::vector<CallWrapper>&& calls)
{
    std::vector<std::unique_ptr<Call>> result {};
    result.reserve(calls.size());
    
    std::transform(std::make_move_iterator(std::begin(calls)),
                   std::make_move_iterator(std::end(calls)),
                   std::back_inserter(result),
                   [] (CallWrapper&& wrapped_call) -> std::unique_ptr<Call>&& {
                       return std::move(wrapped_call.call);
                   });
    
    calls.clear();
    calls.shrink_to_fit();
    
    return result;
}

void set_phase(const SampleIdType& sample, const Phaser::PhaseSet::PhaseRegion& phase,
               const std::vector<GenomicRegion>& call_regions, CallWrapper& call)
{
    const auto overlapped = overlap_range(call_regions, phase.get_region(),
                                          BidirectionallySortedTag {});
    
    assert(!overlapped.empty());
    
    call->set_phase(sample, Call::PhaseCall {overlapped.front(), phase.score});
}

void set_phasing(std::vector<CallWrapper>& calls, const Phaser::PhaseSet& phase_set)
{
    const auto call_regions = extract_regions(calls);
    
    for (auto& call : calls) {
        const auto& call_region = call.get_region();
        
        for (const auto& p : phase_set.phase_regions) {
            const auto& phase = find_phase_region(p.second, call_region);
            if (phase) {
                set_phase(p.first, *phase, call_regions, call);
            }
        }
    }
}

void remove_calls_outside_call_region(std::vector<CallWrapper>& calls,
                                      const GenomicRegion& call_region)
{
    const auto overlapped = overlap_range(calls, call_region);
    calls.erase(overlapped.end().base(), std::end(calls));
    calls.erase(std::begin(calls), overlapped.begin().base());
}

void append_calls(std::deque<VcfRecord>& curr_records, std::vector<CallWrapper>&& new_calls,
                  const VcfRecordFactory& factory)
{
    if (new_calls.empty()) return;
    
    auto new_records = factory.make(unwrap(std::move(new_calls)));
    
    curr_records.insert(std::end(curr_records),
                        std::make_move_iterator(std::begin(new_records)),
                        std::make_move_iterator(std::end(new_records)));
}

std::deque<VcfRecord>
VariantCaller::call(const GenomicRegion& call_region, ProgressMeter& progress_meter) const
{
    resume_timer(init_timer);
    
    ReadMap reads;
    std::deque<VcfRecord> result {};
    
    if (candidate_generator_.requires_reads()) {
        reads = read_pipe_.get().fetch_reads(call_region);
        
        add_reads(reads, candidate_generator_);
        
        if (!refcalls_requested() && all_empty(reads)) {
            if (debug_log_) stream(*debug_log_) << "No reads found in call region";
            return result;
        }
        
        if (debug_log_) {
            stream(*debug_log_) << "There are " << count_reads(reads) << " reads";
        }
    }
    
    const auto candidate_region = calculate_candidate_region(call_region, reads, candidate_generator_);
    
    if (debug_log_) {
        stream(*debug_log_) << "Generating candidates in region " << candidate_region;
    }
    
    auto candidates = generate_candidates(candidate_generator_, candidate_region, reference_);
    
    if (debug_log_) {
        debug::print_final_candidates(stream(*debug_log_), candidates);
    }
    
    if (!refcalls_requested() && candidates.empty()) {
        return result;
    }
    
    if (!candidate_generator_.requires_reads()) {
        // as we didn't fetch them earlier
        reads = read_pipe_.get().fetch_reads(extract_regions(candidates));
    }
    
    HaplotypeGenerator generator {candidate_region, reference_, candidates, reads, max_haplotypes_,
                                    lag_haplotype_generation_};
    Phaser phaser {min_phase_score_};
    
    std::vector<Haplotype> haplotypes;
    GenomicRegion active_region;
    
    auto completed_region = head_region(call_region);
    
    pause_timer(init_timer);
    
    HaplotypeLikelihoodCache haplotype_likelihoods {max_haplotypes_, samples_};
    
    const VcfRecordFactory factory {reference_, reads, samples_, call_sites_only_};
    
    while (true) {
        resume_timer(haplotype_generation_timer);
        std::tie(haplotypes, active_region) = generator.progress();
        pause_timer(haplotype_generation_timer);
        
        if (debug_log_) {
            stream(*debug_log_) << "Active region is " << active_region;
        }
        
        if (is_after(active_region, call_region) || haplotypes.empty()) {
            if (debug_log_) {
                if (haplotypes.empty()) {
                    stream(*debug_log_) << "No haplotypes were generated in the active region";
                } else {
                    stream(*debug_log_) << "Generated " << haplotypes.size()
                                << " haplotypes but active region is after call region";
                }
            }
            progress_meter.log_completed(active_region);
            break;
        }
        
        remove_passed_candidates(candidates, candidate_region, haplotype_region(haplotypes));
        
        if (debug_log_) {
            debug::print_active_candidates(stream(*debug_log_), candidates, active_region);
            stream(*debug_log_) << "Haplotype region is " << haplotype_region(haplotypes);
            debug::print_inactive_flanking_candidates(stream(*debug_log_), candidates, active_region,
                                                      haplotype_region(haplotypes));
        }
        
        const auto active_reads = copy_overlapped(reads, active_region);
        
        if (debug_log_) {
            stream(*debug_log_) << "There are " << haplotypes.size() << " initial haplotypes";
            stream(*debug_log_) << "There are " << count_reads(active_reads) << " active reads";
        }
        
        remove_duplicate_haplotypes(haplotypes);
        
        resume_timer(likelihood_timer);
        haplotype_likelihoods.populate(active_reads, haplotypes,
                                       calculate_flank_state(haplotypes, active_region, candidates));
        pause_timer(likelihood_timer);
        
        if (TRACE_MODE) {
            Logging::TraceLogger trace_log {};
            debug::print_read_haplotype_liklihoods(stream(trace_log), haplotypes, active_reads,
                                                   haplotype_likelihoods, -1);
        }
        
        resume_timer(haplotype_fitler_timer);
        auto removed_haplotypes = filter_to_n_haplotypes(haplotypes, samples_,
                                                         haplotype_likelihoods,
                                                         max_haplotypes_);
        pause_timer(haplotype_fitler_timer);
        
        if (haplotypes.empty()) {
            if (debug_log_) *debug_log_ << "Filtered all haplotypes";
            // This can only happen if all haplotypes have equal likelihood
            generator.clear_progress();
            continue;
        }
        
        if (debug_log_) {
            stream(*debug_log_) << "Filtered " << removed_haplotypes.size() << " haplotypes";
        }
        if (TRACE_MODE) {
            Logging::TraceLogger trace_log {};
            stream(trace_log) << "Filtered " << removed_haplotypes.size() << " haplotypes:";
            debug::print_haplotypes(stream(trace_log), removed_haplotypes,
                                    debug::Resolution::VariantAlleles);
        }
        
        resume_timer(likelihood_timer);
        haplotype_likelihoods.erase(removed_haplotypes);
        pause_timer(likelihood_timer);
        
        resume_timer(haplotype_generation_timer);
        generator.remove(removed_haplotypes);
        generator.uniquely_keep(haplotypes);
        removed_haplotypes.clear();
        pause_timer(haplotype_generation_timer);
        
        if (debug_log_) {
            stream(*debug_log_) << "There are " << haplotypes.size() << " final haplotypes";
        }
        
        resume_timer(latent_timer);
        const auto caller_latents = infer_latents(haplotypes, haplotype_likelihoods);
        pause_timer(latent_timer);
        
        haplotype_likelihoods.clear();
        
        if (TRACE_MODE) {
            Logging::TraceLogger trace_log {};
            debug::print_haplotype_posteriors(stream(trace_log),
                                              *caller_latents->get_haplotype_posteriors(), -1);
        } else if (debug_log_) {
            debug::print_haplotype_posteriors(stream(*debug_log_),
                                              *caller_latents->get_haplotype_posteriors());
        }
        
        resume_timer(phasing_timer);
        const auto phase_set = phaser.try_phase(haplotypes, *caller_latents->get_genotype_posteriors(),
                                                copy_overlapped_to_vector(candidates, haplotype_region(haplotypes)));
        pause_timer(phasing_timer);
        
        if (debug_log_) {
            if (phase_set) {
                debug::print_phase_sets(stream(*debug_log_), *phase_set);
            } else {
                *debug_log_ << "No partial phasings found";
            }
        }
        
        auto unphased_active_region = active_region;
        
        if (overlaps(active_region, call_region) && phase_set) {
            assert(!is_empty(phase_set->region));
            
            if (debug_log_) stream(*debug_log_) << "Phased region is " << phase_set->region;
            
            auto active_candidates = copy_overlapped_to_vector(candidates, phase_set->region);
            
            resume_timer(calling_timer);
            auto variant_calls = wrap(call_variants(active_candidates, *caller_latents));
            
            set_phasing(variant_calls, *phase_set);
            
            remove_calls_outside_call_region(variant_calls, call_region);
            append_calls(result, std::move(variant_calls), factory);
            pause_timer(calling_timer);
            
            auto remaining_active_region = right_overhang_region(active_region, phase_set->region);
            
            resume_timer(haplotype_generation_timer);
            generator.force_forward(remaining_active_region);
            pause_timer(haplotype_generation_timer);
            
            unphased_active_region = right_overhang_region(active_region, phase_set->region);
        }
        
        resume_timer(haplotype_generation_timer);
        auto next_active_region = generator.tell_next_active_region();
        pause_timer(haplotype_generation_timer);
        
        if (!has_passed(next_active_region, active_region)) {
            auto removable_haplotypes = get_removable_haplotypes(haplotypes,
                                                                 *caller_latents->get_haplotype_posteriors(),
                                                                 unphased_active_region);
            
            resume_timer(haplotype_generation_timer);
            generator.remove(removable_haplotypes);
            next_active_region = generator.tell_next_active_region();
            pause_timer(haplotype_generation_timer);
        }
        
        if (begins_before(active_region, next_active_region) && overlaps(active_region, call_region)) {
            auto passed_region   = left_overhang_region(active_region, next_active_region);
            auto uncalled_region = overlapped_region(active_region, passed_region);
            
            if (phase_set && ends_before(phase_set->region, passed_region)) {
                uncalled_region = right_overhang_region(passed_region, phase_set->region);
            }
            
            const auto active_candidates = copy_overlapped_to_vector(candidates, uncalled_region);
            
            std::vector<GenomicRegion> called_regions;
            
            if (!active_candidates.empty()) {
                resume_timer(calling_timer);
                auto variant_calls = wrap(call_variants(active_candidates, *caller_latents));
                pause_timer(calling_timer);
                
                if (!variant_calls.empty()) {
                    called_regions = extract_covered_regions(variant_calls);
                    
                    resume_timer(phasing_timer);
                    const auto phasings = phaser.force_phase(haplotypes,
                                                             *caller_latents->get_genotype_posteriors(),
                                                             active_candidates);
                    pause_timer(phasing_timer);
                    
                    if (debug_log_) debug::print_phase_sets(stream(*debug_log_), phasings);
                    
                    set_phasing(variant_calls, phasings);
                    
                    resume_timer(calling_timer);
                    remove_calls_outside_call_region(variant_calls, call_region);
                    append_calls(result, std::move(variant_calls), factory);
                    pause_timer(calling_timer);
                }
            }
            
            if (refcalls_requested()) {
                auto alleles = generate_candidate_reference_alleles(uncalled_region, active_candidates,
                                                                    called_regions);
                
                auto reference_calls = wrap(call_reference(alleles, *caller_latents, reads));
                
                append_calls(result, std::move(reference_calls), factory);
            }
            
            completed_region = encompassing_region(completed_region, passed_region);
        }
        
        progress_meter.log_completed(active_region);
    }
    
    return result;
}

// private methods

bool VariantCaller::refcalls_requested() const noexcept
{
    return refcall_type_ != RefCallType::None;
}

bool VariantCaller::done_calling(const GenomicRegion& region) const noexcept
{
    return is_empty(region);
}

std::vector<std::reference_wrapper<const Haplotype>>
VariantCaller::get_removable_haplotypes(const std::vector<Haplotype>& haplotypes,
                                        const CallerLatents::HaplotypeProbabilityMap& haplotype_posteriors,
                                        const GenomicRegion& region) const
{
    assert(!haplotypes.empty() && contains(haplotypes.front().get_region(), region));
    
    std::vector<std::reference_wrapper<const Haplotype>> result {};
    result.reserve(haplotypes.size());
    
    for (const auto& p : haplotype_posteriors) {
        if (p.second < min_haplotype_posterior_) {
            result.emplace_back(p.first);
        }
    }
    
    result.shrink_to_fit();
    
    return result;
}

std::vector<Allele>
VariantCaller::generate_callable_alleles(const GenomicRegion& region,
                                         const std::vector<Variant>& candidates) const
{
    using std::begin; using std::end; using std::make_move_iterator; using std::back_inserter;
    
    auto overlapped_candidates = copy_overlapped(candidates, region);
    
    if (is_empty(region) && overlapped_candidates.empty()) return {};
    
    if (overlapped_candidates.empty()) {
        switch (refcall_type_) {
            case RefCallType::Positional:
                return make_positional_reference_alleles(region, reference_);
            case RefCallType::Blocked:
                return std::vector<Allele> {make_reference_allele(region, reference_)};
            default:
                return {};
        }
    }
    
    auto variant_alleles = decompose(overlapped_candidates);
    
    if (refcall_type_ == RefCallType::None) return variant_alleles;
    
    auto covered_regions   = extract_covered_regions(overlapped_candidates);
    auto uncovered_regions = extract_intervening_regions(covered_regions, region);
    
    std::vector<Allele> result {};
    
    if (refcall_type_ == VariantCaller::RefCallType::Blocked) {
        auto reference_alleles = make_reference_alleles(uncovered_regions, reference_);
        result.reserve(reference_alleles.size() + variant_alleles.size());
        std::merge(make_move_iterator(begin(reference_alleles)),
                   make_move_iterator(end(reference_alleles)),
                   make_move_iterator(begin(variant_alleles)),
                   make_move_iterator(end(variant_alleles)),
                   back_inserter(result));
    } else {
        result.reserve(variant_alleles.size() + sum_region_sizes(uncovered_regions));
        
        auto uncovered_itr = begin(uncovered_regions);
        auto uncovered_end = end(uncovered_regions);
        
        for (auto&& variant_allele : variant_alleles) {
            if (uncovered_itr != uncovered_end && begins_before(*uncovered_itr, variant_allele)) {
                auto alleles = make_positional_reference_alleles(*uncovered_itr, reference_);
                result.insert(end(result),
                              make_move_iterator(begin(alleles)),
                              make_move_iterator(end(alleles)));
                std::advance(uncovered_itr, 1);
            }
            result.push_back(std::move(variant_allele));
        }
        
        if (uncovered_itr != uncovered_end) {
            auto alleles = make_positional_reference_alleles(*uncovered_itr, reference_);
            result.insert(end(result),
                          make_move_iterator(begin(alleles)),
                          make_move_iterator(end(alleles)));
        }
    }
    
    return result;
}

template <typename ForwardIt>
ForwardIt find_next(ForwardIt first, ForwardIt last, const Variant& candidate)
{
    return std::find_if_not(first, last,
                            [&] (const Allele& allele) {
                                return is_same_region(allele, candidate);
                            });
}

void append_allele(std::vector<Allele>& alleles, const Allele& allele,
                   const VariantCaller::RefCallType refcall_type)
{
    if (refcall_type == VariantCaller::RefCallType::Blocked && !alleles.empty()
        && are_adjacent(alleles.back(), allele)) {
        alleles.back() = Allele {encompassing_region(alleles.back(), allele),
            alleles.back().get_sequence() + allele.get_sequence()};
    } else {
        alleles.push_back(allele);
    }
}

// TODO: we should catch the case where an insertion has been called and push the refcall
// block up a position, otherwise the returned reference allele (block) will never be called.
std::vector<Allele>
VariantCaller::generate_candidate_reference_alleles(const GenomicRegion& region,
                                                    const std::vector<Variant>& candidates,
                                                    const std::vector<GenomicRegion>& called_regions) const
{
    using std::cbegin; using std::cend;
    
    auto callable_alleles = generate_callable_alleles(region, candidates);
    
    if (callable_alleles.empty() || refcall_type_ == RefCallType::None) return {};
    
    if (candidates.empty()) return callable_alleles;
    
    auto allele_itr        = cbegin(callable_alleles);
    auto allele_end_itr    = cend(callable_alleles);
    auto called_itr        = cbegin(called_regions);
    auto called_end_itr    = cend(called_regions);
    auto candidate_itr     = cbegin(candidates);
    auto candidate_end_itr = cend(candidates);
    
    std::vector<Allele> result {};
    result.reserve(callable_alleles.size());
    
    while (allele_itr != allele_end_itr) {
        if (candidate_itr == candidate_end_itr) {
            append_allele(result, *allele_itr, refcall_type_);
            std::copy(std::next(allele_itr), allele_end_itr, std::back_inserter(result));
            break;
        }
        
        if (called_itr == called_end_itr) {
            append_allele(result, *allele_itr, refcall_type_);
            if (begins_before(*allele_itr, *candidate_itr)) {
                ++allele_itr;
            } else {
                allele_itr = find_next(allele_itr, allele_end_itr, *candidate_itr);
                ++candidate_itr;
            }
        } else {
            if (is_same_region(*called_itr, *candidate_itr)) { // called candidate
                while (is_before(*allele_itr, *called_itr)) {
                    append_allele(result, *allele_itr, refcall_type_);
                    ++allele_itr;
                }
                allele_itr = find_next(allele_itr, allele_end_itr, *candidate_itr);
                ++candidate_itr;
                ++called_itr;
            } else if (begins_before(*called_itr, *candidate_itr)) { // parsimonised called candidate
                if (!overlaps(*allele_itr, *called_itr)) {
                    append_allele(result, *allele_itr, refcall_type_);
                    ++allele_itr;
                } else {
                    if (begins_before(*allele_itr, *called_itr)) { // when variant has been left padded
                        append_allele(result, splice(*allele_itr, left_overhang_region(*allele_itr, *called_itr)),
                                      refcall_type_);
                    }
                    
                    // skip contained alleles and candidates as they include called variants
                    allele_itr    = cend(contained_range(allele_itr, allele_end_itr, *called_itr)).base();
                    candidate_itr = cend(contained_range(candidate_itr, candidate_end_itr, *called_itr)).base();
                    
                    ++called_itr;
                }
            } else {
                append_allele(result, *allele_itr, refcall_type_);
                
                if (begins_before(*allele_itr, *candidate_itr)) {
                    ++allele_itr;
                } else {
                    allele_itr = find_next(allele_itr, allele_end_itr, *candidate_itr);
                    ++candidate_itr;
                }
            }
        }
    }
    
    result.shrink_to_fit();
    
    return result;
}

namespace debug
{
    template <typename S>
    void print_left_aligned_candidates(S&& stream, const std::vector<Variant>& raw_candidates,
                                       const ReferenceGenome& reference)
    {
        std::deque<std::pair<Variant, Variant>> left_aligned {};
        
        for (const auto& raw_candidate : raw_candidates) {
            auto left_aligned_candidate = left_align(raw_candidate, reference);
            
            if (left_aligned_candidate != raw_candidate) {
                left_aligned.emplace_back(raw_candidate, std::move(left_aligned_candidate));
            }
        }
        
        if (left_aligned.empty()) {
            stream << "No candidates were left aligned" << '\n';
        } else {
            if (left_aligned.size() == 1) {
                stream << "1 candidate was left aligned:" << '\n';
            } else {
                stream << left_aligned.size() << " candidates were left aligned:" << '\n';
            }
            for (const auto& p : left_aligned) {
                stream << p.first << " to " << p.second << '\n';
            }
        }
    }
    
    void print_left_aligned_candidates(const std::vector<Variant>& raw_candidates,
                                       const ReferenceGenome& reference)
    {
        print_left_aligned_candidates(std::cout, raw_candidates, reference);
    }
    
    template <typename S>
    void print_final_candidates(S&& stream, const MappableFlatSet<Variant>& candidates, bool number_only)
    {
        if (candidates.empty()) {
            stream << "There are no final candidates" << '\n';
        } else {
            if (candidates.size() == 1) {
                stream << "There is 1 final candidate:" << '\n';
            } else {
                stream << "There are " << candidates.size() << " final candidates:" << '\n';
            }
            
            if (!number_only) {
                for (const auto& c : candidates) stream << c << '\n';
            }
        }
    }
    
    void print_final_candidates(const MappableFlatSet<Variant>& candidates, bool number_only)
    {
        print_final_candidates(std::cout, candidates, number_only);
    }
    
    template <typename S>
    void print_active_candidates(S&& stream, const MappableFlatSet<Variant>& candidates,
                                 const GenomicRegion& active_region, bool number_only)
    {
        const auto active_candidates = overlap_range(candidates, active_region);
        
        if (active_candidates.empty()) {
            stream << "There are no active candidates" << '\n';
        } else {
            const auto num_active = size(active_candidates);
            
            if (num_active == 1) {
                stream << "There is 1 active candidate:" << '\n';
            } else {
                stream << "There are " << num_active << " active candidates:" << '\n';
            }
            
            if (!number_only) {
                for (const auto& c : active_candidates) stream << c << '\n';
            }
        }
    }
    
    void print_active_candidates(const MappableFlatSet<Variant>& candidates,
                                 const GenomicRegion& active_region, bool number_only)
    {
        print_active_candidates(std::cout, candidates, active_region, number_only);
    }
    
    template <typename S>
    void print_inactive_flanking_candidates(S&& stream, const MappableFlatSet<Variant>& candidates,
                                            const GenomicRegion& active_region,
                                            const GenomicRegion& haplotype_region,
                                            bool number_only)
    {
        const auto flanks = calculate_flank_regions(haplotype_region, active_region, candidates);
        
        stream << "Haplotype flank regions are " << flanks.first << " and " << flanks.second << '\n';
        
        const auto lhs_inactive_candidates = overlap_range(candidates, flanks.first);
        const auto rhs_inactive_candidates = overlap_range(candidates, flanks.second);
        
        const auto num_lhs_inactives = size(lhs_inactive_candidates);
        const auto num_rhs_inactives = size(rhs_inactive_candidates);
        
        if (lhs_inactive_candidates.empty()) {
            if (rhs_inactive_candidates.empty()) {
                stream << "There are no inactive flanking candidates" << '\n';
                return;
            }
            
            stream << "There are no lhs inactive flanking candidates" << '\n';
            
            if (num_rhs_inactives == 1) {
                stream << "There is 1 rhs inactive flanking candidates: " << '\n';
            } else {
                stream << "There are " << num_rhs_inactives << " rhs inactive flanking candidates: " << '\n';
            }
            
            if (!number_only) {
                for (const auto& c : rhs_inactive_candidates) stream << c << '\n';
            }
            
            return;
        }
        
        if (num_lhs_inactives == 1) {
            stream << "There is 1 lhs inactive flanking candidates: " << '\n';
        } else {
            stream << "There are " << num_lhs_inactives << " lhs inactive flanking candidates: " << '\n';
        }
        
        if (!number_only) {
            for (const auto& c : lhs_inactive_candidates) stream << c << '\n';
        }
        
        if (rhs_inactive_candidates.empty()) {
            stream << "There are no rhs inactive flanking candidates" << '\n';
        } else {
            if (num_rhs_inactives == 1) {
                stream << "There is 1 rhs inactive flanking candidates: " << '\n';
            } else {
                stream << "There are " << num_rhs_inactives << " rhs inactive flanking candidates: " << '\n';
            }
            
            if (!number_only) {
                for (const auto& c : rhs_inactive_candidates) stream << c << '\n';
            }
        }
    }
    
    void print_inactive_flanking_candidates(const MappableFlatSet<Variant>& candidates,
                                            const GenomicRegion& active_region,
                                            const GenomicRegion& haplotype_region,
                                            bool number_only)
    {
        print_inactive_flanking_candidates(std::cout, candidates, active_region, haplotype_region,
                                           number_only);
    }
    
    template <typename S>
    void print_haplotypes(S&& stream, const std::vector<Haplotype>& haplotypes,
                          Resolution resolution)
    {
        stream << "Printing " << haplotypes.size() << " haplotypes" << '\n';
        
        for (const auto& haplotype : haplotypes) {
            if (resolution == Resolution::Sequence || resolution == Resolution::SequenceAndAlleles
                || resolution == Resolution::SequenceAndVariantAlleles) {
                stream << haplotype << '\n';
            }
            if (resolution == Resolution::Alleles || resolution == Resolution::SequenceAndAlleles) {
                ::debug::print_alleles(stream, haplotype); stream << '\n';
            } else if (resolution != Resolution::Sequence) {
                ::debug::print_variant_alleles(stream, haplotype);
                stream << '\n';
            }
        }
    }
    
    void print_haplotypes(const std::vector<Haplotype>& haplotypes, const Resolution resolution)
    {
        print_haplotypes(std::cout, haplotypes, resolution);
    }
    
    template <typename S, typename Map>
    void print_haplotype_posteriors(S&& stream, const Map& haplotype_posteriors, std::size_t n)
    {
        const auto m = std::min(haplotype_posteriors.size(), n);
        
        if (m == haplotype_posteriors.size()) {
            stream << "Printing all haplotype posteriors" << '\n';
        } else {
            stream << "Printing top " << m << " haplotype posteriors" << '\n';
        }
        
        std::vector<std::pair<std::reference_wrapper<const Haplotype>, double>> v {};
        v.reserve(haplotype_posteriors.size());
        
        std::copy(std::cbegin(haplotype_posteriors), std::cend(haplotype_posteriors),
                  std::back_inserter(v));
        
        const auto mth = std::next(std::begin(v), m);
        
        std::partial_sort(std::begin(v), mth, std::end(v),
                          [] (const auto& lhs, const auto& rhs) {
                              return lhs.second > rhs.second;
                          });
        
        std::for_each(std::begin(v), mth,
                      [&] (const auto& p) {
                          ::debug::print_variant_alleles(stream, p.first);
                          stream << " " << p.second << '\n';
                      });
    }
    
    template <typename Map>
    void print_haplotype_posteriors(const Map& haplotype_posteriors, std::size_t n)
    {
        print_haplotype_posteriors(std::cout, haplotype_posteriors, n);
    }
    
    auto find_read(const std::string& region, const std::string& cigar_str,
                   const ReadContainer& reads)
    {
        const auto cigar = parse_cigar_string(cigar_str);
        return std::find_if(std::cbegin(reads), std::cend(reads),
                            [&] (const AlignedRead& read) {
                                return read.get_cigar_string() == cigar
                                && to_string(mapped_region(read)) == region;
                            });
    }
    
    auto find_read(const SampleIdType& sample, const std::string& region,
                   const std::string& cigar_str, const ReadMap& reads)
    {
        return find_read(region, cigar_str, reads.at(sample));
    }
    
    auto find_first_read(const std::string& region, const std::string& cigar_str,
                         const ReadMap& reads)
    {
        for (const auto& p : reads) {
            const auto it = find_read(region, cigar_str, p.second);
            if (it != std::cend(p.second)) return it;
        }
        return std::cend(std::cbegin(reads)->second);
    }
    
    double calculate_likelihood(const Haplotype& haplotype, const AlignedRead& read,
                                const HaplotypeLikelihoodModel::FlankState flank_state)
    {
        SampleIdType test_sample {"*test-sample*"};
        HaplotypeLikelihoodCache cache {1, {test_sample}};
        ReadContainer sample_reads {};
        sample_reads.emplace(read);
        ReadMap reads {};
        reads.emplace(test_sample, sample_reads);
        cache.populate(reads, {haplotype}, flank_state);
        return cache.log_likelihoods(test_sample, haplotype).front();
    }
    
    void run_likelihood_calculation(const std::string& haplotype_str,
                                    const std::string& haplotype_region,
                                    const std::string& active_region,
                                    const std::string& read_region,
                                    const std::string& cigar_str,
                                    const ReadMap& reads,
                                    const MappableFlatSet<Variant>& candidates,
                                    const ReferenceGenome& reference)
    {
        auto haplotype = ::debug::make_haplotype(haplotype_str, haplotype_region, reference);
        auto flank_state = calculate_flank_state({haplotype},
                                                 parse_region(haplotype_region, reference),
                                                 candidates);
        auto read = *find_first_read(read_region, cigar_str, reads);
        auto likelihood = calculate_likelihood(haplotype, read, flank_state);
        std::cout << likelihood << std::endl;
    }
} // namespace debug
} // namespace Octopus

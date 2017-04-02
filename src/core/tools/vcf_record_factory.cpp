// Copyright (c) 2016 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "vcf_record_factory.hpp"

#include <string>
#include <unordered_set>
#include <set>
#include <unordered_map>
#include <utility>
#include <stdexcept>
#include <algorithm>
#include <functional>
#include <sstream>
#include <iostream>

#include <boost/optional.hpp>

#include "concepts/equitable.hpp"
#include "concepts/mappable.hpp"
#include "basics/genomic_region.hpp"
#include "basics/mappable_reference_wrapper.hpp"
#include "utils/mappable_algorithms.hpp"
#include "core/types/allele.hpp"
#include "utils/read_stats.hpp"
#include "utils/string_utils.hpp"
#include "utils/maths.hpp"
#include "utils/append.hpp"
#include "exceptions/program_error.hpp"
#include "core/types/calls/variant_call.hpp"

#define _unused(x) ((void)(x))

namespace octopus {

VcfRecordFactory::VcfRecordFactory(const ReferenceGenome& reference, const ReadMap& reads,
                                   std::vector<SampleName> samples, bool sites_only)
: reference_ {reference}
, reads_ {reads}
, samples_ {std::move(samples)}
, sites_only_ {sites_only}
{}

namespace {

using CallWrapperReference = MappableReferenceWrapper<CallWrapper>;

bool are_in_phase(const Call::GenotypeCall& lhs, const Call::GenotypeCall& rhs)
{
    return lhs.phase && overlaps(lhs.phase->region(), rhs.genotype);
}

} // namespace

class InconsistentCallError : public ProgramError
{
public:
    InconsistentCallError(SampleName sample, Allele first, Allele second)
    : sample_ {std::move(sample)}
    , first_ {std::move(first)}
    , second_ {std::move(second)}
    {}
private:
    SampleName sample_;
    Allele first_, second_;
        
    std::string do_where() const override
    {
        return "VcfRecordFactory::make";
    }
    std::string do_why() const override
    {
        std::ostringstream ss {};
        ss << "In sample " << sample_ << ", alleles " << first_ << " & " << second_ << " were both called";
        return ss.str();
    }
};

void resolve_indel_genotypes(std::vector<CallWrapper>& calls, const std::vector<SampleName>& samples)
{
    for (auto it = begin(calls); it != end(calls);) {
        if (is_empty(it->mapped_region())) {
            const auto rit = std::find_if_not(make_reverse_iterator(it), make_reverse_iterator(begin(calls)),
                                              [it] (const auto& call) { return are_adjacent(call, *it); });
            // Now everything between rit and it is adjacent to the insertion at it, and will have inserted sequence
            // we want to remove.
            for_each(rit.base(), it, [&samples, it] (auto& call) {
                for (const auto& sample : samples) {
                    const auto& insertion_genotype = (*it)->get_genotype_call(sample).genotype;
                    auto& sample_genotype = call->get_genotype_call(sample).genotype;
                    std::vector<Allele::NucleotideSequence> resolved_alleles {};
                    resolved_alleles.reserve(sample_genotype.ploidy());
                    transform(std::cbegin(sample_genotype), std::cend(sample_genotype),
                              std::cbegin(insertion_genotype), std::back_inserter(resolved_alleles),
                              [&sample] (const Allele& allele1, const Allele& allele2) {
                                  if (is_insertion(allele2)) {
                                      const auto& old_sequence = allele1.sequence();
                                      if (old_sequence.size() <= sequence_size(allele2)) {
                                          throw InconsistentCallError {sample, allele1, allele2};
                                      }
                                      return Allele::NucleotideSequence {
                                      cbegin(old_sequence), prev(cend(old_sequence), sequence_size(allele2))
                                      };
                                  }
                                  return allele1.sequence();
                              });
                    Genotype<Allele> new_genotype {sample_genotype.ploidy()};
                    for (auto& sequence : resolved_alleles) {
                        new_genotype.emplace(Allele {mapped_region(sample_genotype), move(sequence)});
                    }
                    sample_genotype = std::move(new_genotype);
                }
            });
            auto it2 = std::find_if_not(next(it), end(calls),
                                        [it] (const auto& call) {
                                            return call->mapped_region() == it->mapped_region();
                                        });
            if (it2 == end(calls)) break;
            if (!overlaps(*it, *it2)) {
                it = it2;
                continue;
            }
            auto it3 = find_first_after(next(it2), end(calls), *it);
            // Now everything between it and it2 is an insertion, anything between
            // it2 and it3 is another call which will have inserted sequence we want to remove.
            // Note the genotype calls of all insertions must be the same as they are in the
            // same region
            for_each(it2, it3, [&samples, it] (auto& call) {
                for (const auto& sample : samples) {
                    const auto& insertion_genotype = (*it)->get_genotype_call(sample).genotype;
                    auto& sample_genotype = call->get_genotype_call(sample).genotype;
                    std::vector<Allele::NucleotideSequence> resolved_alleles {};
                    resolved_alleles.reserve(sample_genotype.ploidy());
                    transform(std::cbegin(sample_genotype), std::cend(sample_genotype),
                              std::cbegin(insertion_genotype), std::back_inserter(resolved_alleles),
                              [&sample] (const Allele& allele1, const Allele& allele2) {
                                  if (is_insertion(allele2)) {
                                      const auto& old_sequence = allele1.sequence();
                                      if (old_sequence.size() <= sequence_size(allele2)) {
                                          throw InconsistentCallError {sample, allele1, allele2};
                                      }
                                      return Allele::NucleotideSequence {
                                      next(cbegin(old_sequence), sequence_size(allele2)), cend(old_sequence)
                                      };
                                  }
                                  return allele1.sequence();
                              });
                    Genotype<Allele> new_genotype {sample_genotype.ploidy()};
                    for (auto& sequence : resolved_alleles) {
                        new_genotype.emplace(Allele {mapped_region(sample_genotype), move(sequence)});
                    }
                    sample_genotype = std::move(new_genotype);
                }
            });
            it = it3;
        } else {
            ++it;
        }
    }
}

void pad_indels(std::vector<CallWrapper>& calls, const std::vector<SampleName>& samples)
{
    using std::begin; using std::end; using std::move;
    const auto first_modified = std::stable_partition(begin(calls), end(calls),
                                                      [] (const auto& call) { return !call->parsimonise('#'); });
    if (first_modified != end(calls)) {
        const auto last = end(calls);
        const auto first_phase_adjusted = std::partition(first_modified, last,
                                                         [&samples] (const auto& call) {
                                                             return std::none_of(begin(samples), cend(samples),
                                                                                 [&call] (const auto& sample) {
                                                                                     const auto& old_phase = call->get_genotype_call(sample).phase;
                                                                                     return old_phase && begins_before(mapped_region(call), old_phase->region());
                                                                                 });
                                                         });
        if (first_phase_adjusted != last) {
            std::sort(first_phase_adjusted, last);
            for_each(first_phase_adjusted, last,
                     [&samples] (auto& call) {
                         for (const auto& sample : samples) {
                             const auto& old_phase = call->get_genotype_call(sample).phase;
                             if (old_phase && begins_before(mapped_region(call), old_phase->region())) {
                                 auto new_phase_region = expand_lhs(old_phase->region(), 1);
                                 Call::PhaseCall new_phase {move(new_phase_region), old_phase->score()};
                                 call->set_phase(sample, move(new_phase));
                             }
                         }
                     });
            for_each(begin(calls), first_phase_adjusted,
                     [&samples, first_phase_adjusted, last] (auto& call) {
                         for (const auto& sample : samples) {
                             const auto& phase = call->get_genotype_call(sample).phase;
                             if (phase) {
                                 auto overlapped = overlap_range(first_phase_adjusted, last, phase->region());
                                 if (overlapped.empty()) {
                                     overlapped = overlap_range(first_phase_adjusted, last, expand_lhs(phase->region(), 1));
                                     if (!overlapped.empty()) {
                                         if (begin_distance(overlapped.front(), phase->region()) != 1) {
                                             overlapped.advance_begin(1);
                                         }
                                     }
                                 }
                                 if (!overlapped.empty() && overlapped.front() != call) {
                                     const auto& old_phase = call->get_genotype_call(sample).phase;
                                     auto new_phase_region = encompassing_region(overlapped.front(), old_phase->region());
                                     Call::PhaseCall new_phase {move(new_phase_region), old_phase->score()};
                                     call->set_phase(sample, move(new_phase));
                                 }
                             }
                         }
                     });
        }
        std::sort(first_modified, first_phase_adjusted);
        std::inplace_merge(first_modified, first_phase_adjusted, last);
        std::inplace_merge(begin(calls), first_modified, last);
    }
}

std::vector<VcfRecord> VcfRecordFactory::make(std::vector<CallWrapper>&& calls) const
{
    using std::begin; using std::end; using std::cbegin; using std::cend; using std::next;
    using std::prev; using std::for_each; using std::transform; using std::move;
    using std::make_reverse_iterator;
    // TODO: refactor this!!!
    assert(std::is_sorted(std::cbegin(calls), std::cend(calls)));
    resolve_indel_genotypes(calls, samples_);
    pad_indels(calls, samples_);
    std::vector<VcfRecord> result {};
    result.reserve(calls.size());
    for (auto call_itr = begin(calls); call_itr != end(calls);) {
        const auto block_begin_itr = adjacent_overlap_find(call_itr, end(calls));
        transform(std::make_move_iterator(call_itr), std::make_move_iterator(block_begin_itr), std::back_inserter(result),
                  [this] (CallWrapper&& call) {
                      call->replace('#', reference_.fetch_sequence(head_position(call->mapped_region())).front());
                      // We may still have uncalled genotyped alleles here if the called genotype
                      // did not have a high posterior
                      call->replace_uncalled_genotype_alleles(Allele {call->mapped_region(), "."}, 'N');
                      return this->make(move(call.call));
                  });
        if (block_begin_itr == end(calls)) break;
        auto block_end_itr = find_next_mutually_exclusive(block_begin_itr, end(calls));
        const auto block_size = std::distance(block_begin_itr, block_end_itr);
        _unused(block_size);
        assert(block_size > 1);
        auto block_head_end_itr = std::find_if_not(next(block_begin_itr), end(calls),
                                                   [block_begin_itr] (const auto& call) {
                                                       return begins_equal(call, *block_begin_itr);
                                                   });
        const auto alt_itr = std::find_if_not(block_begin_itr, block_head_end_itr,
                                              [] (const auto& call) {
                                                  return call->reference().sequence().front() == '#';
                                              });
        boost::optional<decltype(block_head_end_itr)> base;
        if (alt_itr != block_head_end_itr)  base = alt_itr;
        std::deque<CallWrapper> duplicates {};
        for_each(block_begin_itr, block_head_end_itr, [this, base, &duplicates] (auto& call) {
            assert(!call->reference().sequence().empty());
            if (call->reference().sequence().front() == '#') {
                const auto actual_reference_base = reference_.fetch_sequence(head_position(call)).front();
                auto new_sequence = call->reference().sequence();
                new_sequence.front() = actual_reference_base;
                Allele new_allele {mapped_region(call), move(new_sequence)};
                std::unordered_map<Allele, std::set<Allele>> replacements {};
                call->replace(call->reference(), move(new_allele));
                for (const auto& sample : samples_) {
                    auto& genotype_call = call->get_genotype_call(sample);
                    auto& old_genotype = genotype_call.genotype;
                    const auto ploidy = old_genotype.ploidy();
                    Genotype<Allele> new_genotype {ploidy};
                    for (unsigned i {0}; i < ploidy; ++i) {
                        assert(!old_genotype[i].sequence().empty());
                        if (old_genotype[i].sequence().front() == '#') {
                            auto new_sequence = old_genotype[i].sequence();
                            if (base) {
                                const auto& base_sequence = (**base)->get_genotype_call(sample).genotype[i].sequence();
                                new_sequence.front() = base_sequence.front();
                            } else {
                                new_sequence.front() = actual_reference_base;
                            }
                            Allele new_allele {mapped_region(call), move(new_sequence)};
                            replacements[old_genotype[i]].insert(new_allele);
                            new_genotype.emplace(move(new_allele));
                        } else {
                            new_genotype.emplace(old_genotype[i]);
                        }
                    }
                    old_genotype = move(new_genotype);
                }
                for (auto& p : replacements) {
                    std::transform(std::next(std::cbegin(p.second)), std::cend(p.second), std::back_inserter(duplicates),
                                   [&] (const Allele& replacement) {
                                       auto duplicate = clone(call);
                                       duplicate->replace(p.first, replacement);
                                       return duplicate;
                                   });
                    call->replace(p.first, *std::cbegin(p.second));
                }
            }
        });
        if (std::distance(block_begin_itr, block_head_end_itr) > 1) {
            auto rit3 = next(make_reverse_iterator(block_head_end_itr));
            const auto rit2 = make_reverse_iterator(block_begin_itr);
            for (; rit3 != rit2; ++rit3) {
                auto& curr_call = *rit3;
                for (const auto& sample : samples_) {
                    auto& genotype_call = curr_call->get_genotype_call(sample);
                    auto& old_genotype = genotype_call.genotype;
                    const auto& prev_call = *prev(rit3);
                    const auto& prev_genotype_call = prev_call->get_genotype_call(sample);
                    const auto& prev_genotype = prev_genotype_call.genotype;
                    const auto ploidy = old_genotype.ploidy();
                    Genotype<Allele> new_genotype {ploidy};
                    for (unsigned i {0}; i < ploidy; ++i) {
                        if (prev_genotype[i].sequence() == "*" ||
                            (prev_genotype[i].sequence() == old_genotype[i].sequence()
                             && sequence_size(old_genotype[i]) < region_size(old_genotype))) {
                            Allele::NucleotideSequence new_sequence(1, '*');
                            Allele new_allele {mapped_region(curr_call), move(new_sequence)};
                            new_genotype.emplace(move(new_allele));
                        } else {
                            new_genotype.emplace(old_genotype[i]);
                        }
                    }
                    old_genotype = move(new_genotype);
                }
            }
        }
        
        std::vector<std::vector<const Call*>> prev_represented {};
        prev_represented.reserve(samples_.size());
        for (const auto& sample : samples_) {
            const auto ploidy = block_begin_itr->call->get_genotype_call(sample).genotype.ploidy();
            prev_represented.emplace_back(ploidy, nullptr);
            for (auto itr = block_begin_itr; itr != block_head_end_itr; ++itr) {
                for (unsigned i {0}; i < ploidy; ++i) {
                    if (itr->call->is_represented(itr->call->get_genotype_call(sample).genotype[i])) {
                        prev_represented.back()[i] = std::addressof(*itr->call);
                    }
                }
            }
        }
        
        assert(block_begin_itr < block_head_end_itr);
        for (; block_head_end_itr != block_end_itr; ++block_head_end_itr) {
            auto& curr_call = *block_head_end_itr;
            std::unordered_map<Allele, Allele> replacements {};
            assert(!curr_call->reference().sequence().empty());
            if (curr_call->reference().sequence().front() == '#') {
                const auto actual_reference_base = reference_.fetch_sequence(head_position(curr_call)).front();
                auto new_ref_sequence = curr_call->reference().sequence();
                new_ref_sequence.front() = actual_reference_base;
                Allele new_ref_allele {mapped_region(curr_call), move(new_ref_sequence)};
                curr_call->replace(curr_call->reference(), move(new_ref_allele));
                for (unsigned s {0}; s < samples_.size(); ++s) {
                    const auto& sample = samples_[s];
                    auto& genotype_call = curr_call->get_genotype_call(sample);
                    auto& old_genotype = genotype_call.genotype;
                    const auto ploidy = old_genotype.ploidy();
                    Genotype<Allele> new_genotype {ploidy};
                    for (unsigned i {0}; i < ploidy; ++i) {
                        if (old_genotype[i].sequence().empty()) {
                            Allele::NucleotideSequence new_sequence(region_size(curr_call), '*');
                            Allele new_allele {mapped_region(curr_call), move(new_sequence)};
                            new_genotype.emplace(move(new_allele));
                        } else if (old_genotype[i].sequence().front() == '#') {
                            if (prev_represented[s][i] && begins_before(*prev_represented[s][i], curr_call)) {
                                const auto& prev_represented_genotype = prev_represented[s][i]->get_genotype_call(sample);
                                if (are_in_phase(genotype_call, prev_represented_genotype)) {
                                    const auto& prev_allele = prev_represented_genotype.genotype[i];
                                    const auto overlap = overlapped_region(prev_allele, curr_call);
                                    if (overlap) {
                                        auto new_sequence = old_genotype[i].sequence();
                                        const auto overlap_size = static_cast<std::size_t>(region_size(*overlap));
                                        std::fill_n(std::begin(new_sequence), std::min(overlap_size, new_sequence.size()), '*');
                                        Allele new_allele {mapped_region(curr_call), move(new_sequence)};
                                        replacements.emplace(old_genotype[i], new_allele);
                                        new_genotype.emplace(move(new_allele));
                                    } else {
                                        auto new_sequence = old_genotype[i].sequence();
                                        assert(!old_genotype[i].sequence().empty());
                                        new_sequence.front() = actual_reference_base;
                                        Allele new_allele {mapped_region(curr_call), move(new_sequence)};
                                        replacements.emplace(old_genotype[i], new_allele);
                                        new_genotype.emplace(move(new_allele));
                                    }
                                } else {
                                    auto new_sequence = old_genotype[i].sequence();
                                    assert(!old_genotype[i].sequence().empty());
                                    new_sequence.front() = actual_reference_base;
                                    Allele new_allele {mapped_region(curr_call), move(new_sequence)};
                                    replacements.emplace(old_genotype[i], new_allele);
                                    new_genotype.emplace(move(new_allele));
                                }
                            } else {
                                auto new_sequence = old_genotype[i].sequence();
                                assert(!old_genotype[i].sequence().empty());
                                new_sequence.front() = actual_reference_base;
                                Allele new_allele {mapped_region(curr_call), move(new_sequence)};
                                replacements.emplace(old_genotype[i], new_allele);
                                new_genotype.emplace(move(new_allele));
                            }
                        } else {
                            new_genotype.emplace(old_genotype[i]);
                        }
                    }
                    old_genotype = move(new_genotype);
                }
            } else {
                for (unsigned s {0}; s < samples_.size(); ++s) {
                    auto& genotype_call = curr_call->get_genotype_call(samples_[s]);
                    auto& old_genotype = genotype_call.genotype;
                    const auto ploidy = old_genotype.ploidy();
                    Genotype<Allele> new_genotype {ploidy};
                    for (unsigned i {0}; i < ploidy; ++i) {
                        if (old_genotype[i].sequence().empty()) {
                            Allele::NucleotideSequence new_sequence(region_size(curr_call), '*');
                            Allele new_allele {mapped_region(curr_call), move(new_sequence)};
                            new_genotype.emplace(move(new_allele));
                        } else {
                            new_genotype.emplace(old_genotype[i]);
                        }
                    }
                    old_genotype = move(new_genotype);
                }
            }
            for (auto& p : replacements) {
                curr_call->replace(p.first, p.second);
            }
            for (unsigned s {0}; s < samples_.size(); ++s) {
                const auto& new_genotype = block_head_end_itr->call->get_genotype_call(samples_[s]).genotype;
                for (unsigned i {0}; i < new_genotype.ploidy(); ++i) {
                    const auto& seq = new_genotype[i].sequence();
                    if (std::find(std::cbegin(seq), std::cend(seq), '*') == std::cend(seq)
                        && block_head_end_itr->call->is_represented(new_genotype[i])) {
                        prev_represented[s][i] = std::addressof(*block_head_end_itr->call);
                    }
                }
            }
        }
        for_each(block_begin_itr, block_end_itr, [] (auto& call) {
            call->replace_uncalled_genotype_alleles(Allele {call->mapped_region(), "."}, '*');
        });
        // At this point, all genotypes fields contain canonical bases, '.', or '*', but not '#'.
        std::vector<std::vector<CallWrapper>> segements;
        if (duplicates.empty()) {
            segements = segment_by_begin_copy(std::make_move_iterator(block_begin_itr), std::make_move_iterator(block_end_itr));
        } else {
            std::vector<CallWrapper> section {std::make_move_iterator(block_begin_itr), std::make_move_iterator(block_end_itr)};
            auto itr = utils::append(std::move(duplicates), section);
            std::inplace_merge(std::begin(section), itr, std::end(section));
            segements = segment_by_begin_move(section);
        }
        for (auto&& segment : segements) {
            for (auto&& new_segment : segment_by_end_move(segment)) {
                std::vector<std::unique_ptr<Call>> final_segment {};
                transform(std::make_move_iterator(begin(new_segment)), std::make_move_iterator(end(new_segment)),
                          std::back_inserter(final_segment),
                          [] (auto&& call) -> std::unique_ptr<Call>&& { return move(call.call); });
                result.emplace_back(this->make_segment(move(final_segment)));
            }
        }
        call_itr = block_end_itr;
    }
    return result;
}

// private methods

std::vector<VcfRecord::NucleotideSequence>
extract_all_genotyped_alleles(const Call* call, const std::vector<SampleName>& samples)
{
    using std::begin; using std::end; using std::cbegin; using std::cend;
    
    std::vector<VcfRecord::NucleotideSequence> result {};
    
    for (const auto& sample : samples) {
        const auto& called_genotype = call->get_genotype_call(sample).genotype;
        std::transform(cbegin(called_genotype), cend(called_genotype),
                       std::back_inserter(result), [] (const Allele& allele) {
                           auto result = allele.sequence();
                           std::replace(begin(result), end(result), '*', '~');
                           return result;
                       });
    }
    
    const VcfRecord::NucleotideSequence missing_allele(1, '.');
    auto it = std::remove(begin(result), end(result), missing_allele);
    result.erase(it, end(result));
    std::sort(begin(result), end(result));
    it = std::unique(begin(result), end(result));
    result.erase(it, end(result));
    
    for (auto& alt : result) {
        std::replace(begin(alt), end(alt), '~', '*');
    }
    
    return result;
}

void set_alt_alleles(const Call* call, VcfRecord::Builder& record,
                     const std::vector<SampleName>& samples)
{
    auto alts = extract_all_genotyped_alleles(call, samples);
    auto it = std::find(std::begin(alts), std::end(alts), call->reference().sequence());
    if (it != std::end(alts)) alts.erase(it);
    assert(std::find(std::cbegin(alts), std::cend(alts), "#") == std::cend(alts));
    assert(std::find(std::cbegin(alts), std::cend(alts), ".") == std::cend(alts));
    assert(std::find(std::cbegin(alts), std::cend(alts), "") == std::cend(alts));
    record.set_alt(std::move(alts));
}

void set_vcf_genotype(const SampleName& sample, const Call::GenotypeCall& genotype_call,
                      VcfRecord::Builder& record)
{
    std::vector<VcfRecord::NucleotideSequence> result {};
    result.reserve(genotype_call.genotype.ploidy());
    for (const auto& allele : genotype_call.genotype) {
        result.push_back(allele.sequence());
    }
    record.set_genotype(sample, result, VcfRecord::Builder::Phasing::phased);
}

VcfRecord VcfRecordFactory::make(std::unique_ptr<Call> call) const
{
    auto result = VcfRecord::Builder {};
    const auto& region = call->mapped_region();
    
    result.set_chrom(contig_name(region));
    result.set_pos(mapped_begin(region) + 1);
    result.set_ref(call->reference().sequence());
    set_alt_alleles(call.get(), result, samples_);
    result.set_qual(std::min(5000.0, maths::round(call->quality().score(), 2)));
    const auto call_reads = copy_overlapped(reads_, region);
    result.set_info("NS",  count_samples_with_coverage(call_reads));
    result.set_info("DP",  sum_max_coverages(call_reads));
    result.set_info("SB",  utils::to_string(strand_bias(call_reads), 2));
    result.set_info("BQ",  static_cast<unsigned>(rmq_base_quality(call_reads)));
    result.set_info("MQ",  static_cast<unsigned>(rmq_mapping_quality(call_reads)));
    result.set_info("MQ0", count_mapq_zero(call_reads));
    
    if (call->model_posterior()) {
        result.set_info("MP",  maths::round(call->model_posterior()->score(), 2));
    }
    if (!sites_only_) {
        if (call->all_phased()) {
            result.set_format({"GT", "GQ", "DP", "BQ", "MQ", "PS", "PQ"});
        } else {
            result.set_format({"GT", "GQ", "DP", "BQ", "MQ"});
        }
        
        for (const auto& sample : samples_) {
            const auto& genotype_call = call->get_genotype_call(sample);
            auto gq = std::min(999, static_cast<int>(std::round(genotype_call.posterior.score())));
            
            set_vcf_genotype(sample, genotype_call, result);
            result.set_format(sample, "GQ", std::to_string(gq));
            result.set_format(sample, "DP", max_coverage(call_reads.at(sample)));
            result.set_format(sample, "BQ", static_cast<unsigned>(rmq_base_quality(call_reads.at(sample))));
            result.set_format(sample, "MQ", static_cast<unsigned>(rmq_mapping_quality(call_reads.at(sample))));
            if (call->is_phased(sample)) {
                const auto& phase = *genotype_call.phase;
                auto pq = std::min(99, static_cast<int>(std::round(phase.score().score())));
                result.set_format(sample, "PS", mapped_begin(phase.region()) + 1);
                result.set_format(sample, "PQ", std::to_string(pq));
            }
        }
    }
    
    call->decorate(result);
    
    return result.build_once();
}

namespace {

boost::optional<double> get_model_posterior(const std::vector<std::unique_ptr<Call>>& calls)
{
    std::vector<double> model_posteriors {};
    model_posteriors.reserve(calls.size());
    for (const auto& call : calls) {
        const auto call_model_posterior = call->model_posterior();
        if (call_model_posterior) model_posteriors.push_back(call_model_posterior->score());
    }
    if (model_posteriors.empty()) {
        return boost::none;
    } else {
        return *std::max_element(std::cbegin(model_posteriors), std::cend(model_posteriors));
    }
}

} // namespace

VcfRecord VcfRecordFactory::make_segment(std::vector<std::unique_ptr<Call>>&& calls) const
{
    assert(!calls.empty());
    
    if (calls.size() == 1) {
        return make(std::move(calls.front()));
    }
    
    auto result = VcfRecord::Builder {};
    const auto& region = calls.front()->mapped_region();
    const auto& ref = calls.front()->reference().sequence();
    
    result.set_chrom(contig_name(region));
    result.set_pos(mapped_begin(region) + 1);
    result.set_ref(ref);
    
    std::vector<std::vector<VcfRecord::NucleotideSequence>> resolved_genotypes {};
    resolved_genotypes.reserve(samples_.size());
    
    for (const auto& sample : samples_) {
        const auto ploidy = calls.front()->get_genotype_call(sample).genotype.ploidy();
        std::vector<VcfRecord::NucleotideSequence> resolved_sample_genotype(ploidy);
        const auto& first_called_genotype = calls.front()->get_genotype_call(sample).genotype;
        std::transform(std::cbegin(first_called_genotype), std::cend(first_called_genotype),
                       std::begin(resolved_sample_genotype),
                       [] (const Allele& allele) { return allele.sequence(); });
        std::for_each(std::next(std::cbegin(calls)), std::cend(calls), [&] (const auto& call) {
            const auto& called_genotype = call->get_genotype_call(sample).genotype;
            std::transform(std::cbegin(called_genotype), std::cend(called_genotype),
                           std::cbegin(resolved_sample_genotype), std::begin(resolved_sample_genotype),
                           [&ref] (const Allele& allele, const auto& curr) {
                               const auto& seq = allele.sequence();
                               if (seq.size() < curr.size()
                                   || (!seq.empty() && (seq.front() == '.' || seq.front() == '*' || seq == ref))) {
                                   return curr;
                               }
                               return seq;
                           });
        });
        resolved_genotypes.push_back(std::move(resolved_sample_genotype));
    }
    
    std::vector<VcfRecord::NucleotideSequence> alt_alleles {};
    for (const auto& genotype : resolved_genotypes) {
        alt_alleles.insert(std::end(alt_alleles), std::cbegin(genotype), std::cend(genotype));
    }
    const VcfRecord::NucleotideSequence missing_allele(1, '.');
    auto it = std::remove(std::begin(alt_alleles), std::end(alt_alleles), missing_allele);
    it = std::remove(std::begin(alt_alleles), it, calls.front()->reference().sequence());
    alt_alleles.erase(it, std::end(alt_alleles));
    std::sort(std::begin(alt_alleles), std::end(alt_alleles));
    it = std::unique(std::begin(alt_alleles), std::end(alt_alleles));
    alt_alleles.erase(it, std::end(alt_alleles));
    result.set_alt(std::move(alt_alleles));
    auto q = std::min_element(std::cbegin(calls), std::cend(calls),
                              [] (const auto& lhs, const auto& rhs) { return lhs->quality() < rhs->quality(); });
    result.set_qual(std::min(5000.0, maths::round(q->get()->quality().score(), 2)));
    result.set_info("NS",  count_samples_with_coverage(reads_, region));
    result.set_info("DP",  sum_max_coverages(reads_, region));
    result.set_info("SB",  utils::to_string(strand_bias(reads_, region), 2));
    result.set_info("BQ",  static_cast<unsigned>(rmq_base_quality(reads_, region)));
    result.set_info("MQ",  static_cast<unsigned>(rmq_mapping_quality(reads_, region)));
    result.set_info("MQ0", count_mapq_zero(reads_, region));
    
    const auto mp = get_model_posterior(calls);
    if (mp) {
        result.set_info("MP", maths::round(*mp, 2));
    }
    if (!sites_only_) {
        if (calls.front()->all_phased()) {
            result.set_format({"GT", "GQ", "DP", "BQ", "MQ", "PS", "PQ"});
        } else {
            result.set_format({"GT", "GQ", "DP", "BQ", "MQ"});
        }
        
        auto sample_itr = std::begin(resolved_genotypes);
        for (const auto& sample : samples_) {
            const auto posterior = calls.front()->get_genotype_call(sample).posterior;
            auto gq = std::min(999, static_cast<int>(std::round(posterior.score())));
            
            result.set_genotype(sample, *sample_itr++, VcfRecord::Builder::Phasing::phased);
            result.set_format(sample, "GQ", std::to_string(gq));
            result.set_format(sample, "DP", max_coverage(reads_.at(sample), region));
            result.set_format(sample, "BQ", static_cast<unsigned>(rmq_base_quality(reads_.at(sample), region)));
            result.set_format(sample, "MQ", static_cast<unsigned>(rmq_mapping_quality(reads_.at(sample), region)));
            if (calls.front()->is_phased(sample)) {
                const auto phase = *calls.front()->get_genotype_call(sample).phase;
                auto pq = std::min(99, static_cast<int>(std::round(phase.score().score())));
                result.set_format(sample, "PS", mapped_begin(phase.region()) + 1);
                result.set_format(sample, "PQ", std::to_string(pq));
            }
        }
    }
    
    for (const auto& call : calls) {
        call->decorate(result);
    }
    
    return result.build_once();
}
    
} // namespace octopus

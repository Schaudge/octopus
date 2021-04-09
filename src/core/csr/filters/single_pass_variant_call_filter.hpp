// Copyright (c) 2015-2021 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef single_pass_variant_call_filter_hpp
#define single_pass_variant_call_filter_hpp

#include <vector>

#include <boost/optional.hpp>

#include "logging/progress_meter.hpp"
#include "basics/genomic_region.hpp"
#include "variant_call_filter.hpp"

namespace octopus {namespace csr {

class SinglePassVariantCallFilter : public VariantCallFilter
{
public:
    SinglePassVariantCallFilter() = delete;
    
    SinglePassVariantCallFilter(FacetFactory facet_factory,
                                std::vector<MeasureWrapper> measures,
                                OutputOptions output_config,
                                ConcurrencyPolicy threading,
                                boost::optional<ProgressMeter&> progress);
    
    SinglePassVariantCallFilter(const SinglePassVariantCallFilter&)            = delete;
    SinglePassVariantCallFilter& operator=(const SinglePassVariantCallFilter&) = delete;
    SinglePassVariantCallFilter(SinglePassVariantCallFilter&&)                 = delete;
    SinglePassVariantCallFilter& operator=(SinglePassVariantCallFilter&&)      = delete;
    
    virtual ~SinglePassVariantCallFilter() override = default;
    
protected:
    std::vector<std::string> measure_names_;
    
private:
    boost::optional<ProgressMeter&> progress_;
    mutable boost::optional<GenomicRegion::ContigName> current_contig_;
    
    virtual Classification classify(const MeasureVector& call_measures) const = 0;
    
    void filter(const VcfReader& source, VcfWriter& dest, const VcfHeader& dest_header) const override;
    void filter(const VcfRecord& call, VcfWriter& dest, const VcfHeader& dest_header, const SampleList& samples) const;
    void filter(const CallBlock& block, VcfWriter& dest, const VcfHeader& dest_header, const SampleList& samples) const;
    void filter(const std::vector<CallBlock>& blocks, VcfWriter& dest, const VcfHeader& dest_header, const SampleList& samples) const;
    void filter(const CallBlock& block, const MeasureBlock & measures, VcfWriter& dest, const VcfHeader& dest_header, const SampleList& samples) const;
    void filter(const VcfRecord& call, const MeasureVector& measures, VcfWriter& dest, const VcfHeader& dest_header, const SampleList& samples) const;
    ClassificationList classify(const MeasureVector& call_measures, const SampleList& samples) const;
    void log_progress(const GenomicRegion& region) const;
};

} // namespace csr
} // namespace octopus

#endif

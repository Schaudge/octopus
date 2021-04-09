// Copyright (c) 2015-2021 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef variant_call_filter_factory_hpp
#define variant_call_filter_factory_hpp

#include <memory>
#include <utility>

#include <boost/optional.hpp>

#include "logging/progress_meter.hpp"
#include "io/variant/vcf_header.hpp"
#include "variant_call_filter.hpp"

namespace octopus {

class ReferenceGenome;
class BufferedReadPipe;
class PloidyMap;
class Pedigree;

namespace csr {

class FacetFactory;

class VariantCallFilterFactory
{
public:
    VariantCallFilterFactory() = default;
    
    VariantCallFilterFactory(VariantCallFilter::OutputOptions output_options);
    
    VariantCallFilterFactory(const VariantCallFilterFactory&)            = default;
    VariantCallFilterFactory& operator=(const VariantCallFilterFactory&) = default;
    VariantCallFilterFactory(VariantCallFilterFactory&&)                 = default;
    VariantCallFilterFactory& operator=(VariantCallFilterFactory&&)      = default;
    
    virtual ~VariantCallFilterFactory() = default;
    
    std::unique_ptr<VariantCallFilterFactory> clone() const;
    
    void set_output_options(VariantCallFilter::OutputOptions output_options);
    
    std::unique_ptr<VariantCallFilter>
    make(const ReferenceGenome& reference,
         BufferedReadPipe read_pipe,
         VcfHeader input_header,
         PloidyMap ploidies,
         HaplotypeLikelihoodModel likelihood_model,
         boost::optional<Pedigree> pedigree,
         VariantCallFilter::OutputOptions output_config,
         boost::optional<ProgressMeter&> progress = boost::none,
         boost::optional<unsigned> max_threads = 1) const;
    
    std::unique_ptr<VariantCallFilter>
    make(const ReferenceGenome& reference,
         BufferedReadPipe read_pipe,
         VcfHeader input_header,
         PloidyMap ploidies,
         HaplotypeLikelihoodModel likelihood_model,
         boost::optional<Pedigree> pedigree,
         boost::optional<ProgressMeter&> progress = boost::none,
         boost::optional<unsigned> max_threads = 1) const;
    
private:
    VariantCallFilter::OutputOptions output_options_;
    
    virtual std::unique_ptr<VariantCallFilterFactory> do_clone() const = 0;
    virtual
    std::unique_ptr<VariantCallFilter>
    do_make(FacetFactory facet_factory,
            VariantCallFilter::OutputOptions output_config,
            boost::optional<ProgressMeter&> progress,
            VariantCallFilter::ConcurrencyPolicy threading) const = 0;
};

} // namespace csr

using csr::VariantCallFilterFactory;

} // namespace octopus

#endif

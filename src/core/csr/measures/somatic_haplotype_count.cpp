// Copyright (c) 2015-2019 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "somatic_haplotype_count.hpp"

#include "io/variant/vcf_record.hpp"
#include "../facets/samples.hpp"
#include "../facets/ploidies.hpp"
#include "measure.hpp"

namespace octopus { namespace csr {

const std::string SomaticHaplotypeCount::name_ = "SHC";

std::unique_ptr<Measure> SomaticHaplotypeCount::do_clone() const
{
    return std::make_unique<SomaticHaplotypeCount>(*this);
}

Measure::ResultType SomaticHaplotypeCount::get_default_result() const
{
    return boost::optional<std::vector<int>> {};
}

Measure::ResultType SomaticHaplotypeCount::do_evaluate(const VcfRecord& call, const FacetMap& facets) const
{
    boost::optional<std::vector<int>> result {};
    if (is_somatic(call)) {
        const auto& samples = get_value<Samples>(facets.at("Samples"));
        const auto& ploidies = get_value<Ploidies>(facets.at("Ploidies"));
        result = std::vector<int> {};
        result->reserve(samples.size());
        for (const auto& sample : samples) {
            result->push_back(call.ploidy(sample) - ploidies.at(sample));
        }
    }
    return result;
}

Measure::ResultCardinality SomaticHaplotypeCount::do_cardinality() const noexcept
{
    return ResultCardinality::num_samples;
}

const std::string& SomaticHaplotypeCount::do_name() const
{
    return name_;
}

std::string SomaticHaplotypeCount::do_describe() const
{
    return "Number of called somatic haplotypes";
}

std::vector<std::string> SomaticHaplotypeCount::do_requirements() const
{
    return {"Samples", "Ploidies"};
}

} // namespace csr
} // namespace octopus

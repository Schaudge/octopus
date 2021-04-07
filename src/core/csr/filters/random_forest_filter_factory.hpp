// Copyright (c) 2015-2021 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef random_forest_filter_factory_hpp
#define random_forest_filter_factory_hpp

#include <memory>
#include <vector>
#include <string>

#include <boost/optional.hpp>
#include <boost/filesystem.hpp>

#include "basics/phred.hpp"
#include "logging/progress_meter.hpp"
#include "variant_call_filter_factory.hpp"
#include "variant_call_filter.hpp"
#include "random_forest_filter.hpp"

namespace octopus { namespace csr {

class FacetFactory;

class RandomForestFilterFactory : public VariantCallFilterFactory
{
public:
    using Path = RandomForestFilter::Path;
    enum class ForestType { germline, somatic, denovo };
    
    struct Options : public RandomForestFilter::Options
    {
        Options() = default;
        Options(RandomForestFilter::Options common);
        
        bool use_somatic_forest_for_refcalls;
    };
    
    RandomForestFilterFactory();
    
    RandomForestFilterFactory(std::vector<Path> ranger_forests,
                              std::vector<ForestType> forest_types,
                              Path temp_directory,
                              Options options = Options {});
    
    RandomForestFilterFactory(const RandomForestFilterFactory&)            = default;
    RandomForestFilterFactory& operator=(const RandomForestFilterFactory&) = default;
    RandomForestFilterFactory(RandomForestFilterFactory&&)                 = default;
    RandomForestFilterFactory& operator=(RandomForestFilterFactory&&)      = default;
    
    ~RandomForestFilterFactory() = default;
    
private:
    std::vector<Path> ranger_forests_;
    std::vector<ForestType> forest_types_;
    Path temp_directory_;
    Options options_;
    
    std::unique_ptr<VariantCallFilterFactory> do_clone() const override;
    std::unique_ptr<VariantCallFilter>
    do_make(FacetFactory facet_factory,
            VariantCallFilter::OutputOptions output_config,
            boost::optional<ProgressMeter&> progress,
            VariantCallFilter::ConcurrencyPolicy threading) const override;
};

} // namespace csr

using csr::RandomForestFilterFactory;

} // namespace octopus

#endif

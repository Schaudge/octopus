//
//  variant_factory.h
//  Octopus
//
//  Created by Daniel Cooke on 05/02/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#ifndef __Octopus__variant_factory__
#define __Octopus__variant_factory__

#include "variant.h"
#include "utils.h"

class VariantFactory
{
public:
    using SizeType = Variant::SizeType;
    
    VariantFactory() = default;
    ~VariantFactory() = default;
    
    VariantFactory(const VariantFactory&)            = delete;
    VariantFactory& operator=(const VariantFactory&) = delete;
    VariantFactory(VariantFactory&&)                 = delete;
    VariantFactory& operator=(VariantFactory&&)      = delete;
    
    template <typename T1, typename T2, typename T3>
    Variant make(T1&& the_reference_allele_region, T2&& the_reference_allele,
                 T3&& the_alternative_allele) const;
    
//    template <typename T1, typename T2, typename T3>
//    Variant make(T1&& the_contig_name, SizeType the_reference_allele_begin,
//                 T2&& the_reference_allele, T3&& the_alternative_allele);
};

template <typename T1, typename T2, typename T3>
Variant VariantFactory::make(T1&& the_reference_allele_region, T2&& the_reference_allele,
                             T3&& the_alternative_allele) const
{
    std::function<double()> prior_model {};
    if (stringlen(the_reference_allele) == stringlen(the_alternative_allele)) {
        if (stringlen(the_alternative_allele) == 1) {
            prior_model = [] () { return 1e-5; };
        } else {
            prior_model = [] () { return 1e-6; };
        }
    } else {
        if (stringlen(the_reference_allele) < stringlen(the_alternative_allele)) {
            prior_model = [] () { return 1e-7; };
        } else {
            prior_model = [] () { return 1e-8; };
        }
    }
    return Variant {
        std::forward<T1>(the_reference_allele_region),
        std::forward<T2>(the_reference_allele),
        std::forward<T3>(the_alternative_allele),
        prior_model
    };
}

//template <typename T1, typename T2, typename T3>
//Variant VariantFactory::make(T1&& the_contig_name, SizeType the_reference_allele_begin,
//                             T2&& the_reference_allele, T3&& the_alternative_allele)
//{
//    
//}

#endif /* defined(__Octopus__variant_factory__) */

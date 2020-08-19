// Copyright (c) 2015-2020 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef denovo_reference_reversion_call_hpp
#define denovo_reference_reversion_call_hpp

#include <utility>

#include "denovo_call.hpp"

namespace octopus {

class DenovoReferenceReversionCall : public DenovoCall
{
public:
    using VariantCall::GenotypeCall;
    using VariantCall::PhaseCall;
    using DenovoCall::parsimonise;
    
    DenovoReferenceReversionCall() = delete;
    
    template <typename A, typename T>
    DenovoReferenceReversionCall(A&& allele, T&& genotype_calls, Phred<double> quality, Phred<double> posterior);
    
    DenovoReferenceReversionCall(const DenovoReferenceReversionCall&)            = default;
    DenovoReferenceReversionCall& operator=(const DenovoReferenceReversionCall&) = default;
    DenovoReferenceReversionCall(DenovoReferenceReversionCall&&)                 = default;
    DenovoReferenceReversionCall& operator=(DenovoReferenceReversionCall&&)      = default;
    
    virtual ~DenovoReferenceReversionCall() = default;
    
    virtual bool parsimonise(char dummy_base) override;
    
    virtual void decorate(VcfRecord::Builder& record) const override;
    
private:
    virtual std::unique_ptr<Call> do_clone() const override;
};

template <typename A, typename T>
DenovoReferenceReversionCall::DenovoReferenceReversionCall(A&& allele, T&& genotype_calls,
                                                           Phred<double> quality, Phred<double> posterior)
: DenovoCall {Variant {allele, std::forward<A>(allele)}, std::forward<T>(genotype_calls), quality, posterior}
{}

} // namespace octopus

#endif
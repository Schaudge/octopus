// Copyright (c) 2015-2021 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "snv_error_model.hpp"

namespace octopus {

std::unique_ptr<SnvErrorModel> SnvErrorModel::clone() const
{
    return do_clone();
}

void SnvErrorModel::evaluate(const Haplotype& haplotype,
                             MutationVector& forward_snv_mask, PenaltyVector& forward_snv_priors,
                             MutationVector& reverse_snv_mask, PenaltyVector& reverse_snv_priors) const
{
    do_evaluate(haplotype, forward_snv_mask, forward_snv_priors, reverse_snv_mask, reverse_snv_priors);
}

} // namespace octopus

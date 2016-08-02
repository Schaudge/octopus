//
//  read_transform.hpp
//  Octopus
//
//  Created by Daniel Cooke on 07/03/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#ifndef Octopus_read_transform_hpp
#define Octopus_read_transform_hpp

#include "aligned_read.hpp"

namespace octopus { namespace readpipe
{

struct CapBaseQualities
{
    using BaseQuality = AlignedRead::BaseQuality;
    
    CapBaseQualities() = default;
    
    explicit CapBaseQualities(BaseQuality max);
    
    void operator()(AlignedRead& read) const noexcept;
    
private:
    BaseQuality max_;
};

struct MaskOverlappedSegment
{
    void operator()(AlignedRead& read) const noexcept;
};

struct MaskAdapters
{
    void operator()(AlignedRead& read) const noexcept;
};

struct MaskTail
{
    using Length = AlignedRead::NucleotideSequence::size_type;
    
    MaskTail() = default;
    
    explicit MaskTail(Length num_bases);
    
    void operator()(AlignedRead& read) const noexcept;
    
private:
    const Length num_bases_;
};

struct MaskSoftClipped
{
    void operator()(AlignedRead& read) const noexcept;
};

struct MaskSoftClippedBoundries
{
    using Length = AlignedRead::NucleotideSequence::size_type;
    
    MaskSoftClippedBoundries() = default;
    
    explicit MaskSoftClippedBoundries(Length num_bases);
    
    void operator()(AlignedRead& read) const noexcept;
    
private:
    const Length num_bases_;
};

struct QualityAdjustedSoftClippedMasker
{
    void operator()(AlignedRead& read) const noexcept;
};

} // namespace readpipe
} // namespace octopus

#endif

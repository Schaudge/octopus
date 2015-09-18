//
//  read_filter_tests.cpp
//  Octopus
//
//  Created by Daniel Cooke on 07/03/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#define BOOST_TEST_DYN_LINK

#include <boost/test/unit_test.hpp>

#include <iostream>
#include <vector>
#include <algorithm> // std::sort
#include <iterator>  // std::back_inserter

#include "test_common.h"
#include "genomic_region.h"
#include "read_manager.h"
#include "read_filter.h"
#include "read_filters.h"
#include "context_iterators.h"
//#include "read_filter_factory.h"

using Octopus::ContextBackInserter;

BOOST_AUTO_TEST_SUITE(Components)

BOOST_AUTO_TEST_CASE(read_filter_test)
{
    ReadManager a_read_manager {human_1000g_bam1};
    
    auto sample_ids = a_read_manager.get_samples();
    
    auto the_sample_id = sample_ids.at(0);
    
    GenomicRegion a_region {"X", 1000000, 1010000};
    
    auto some_reads = a_read_manager.fetch_reads(the_sample_id, a_region);
    
    if (!std::is_sorted(some_reads.begin(), some_reads.end())) {
        std::sort(some_reads.begin(), some_reads.end());
    }
    
    BOOST_CHECK(some_reads.size() == 669);
    
    using ReadIterator = std::vector<AlignedRead>::const_iterator;
    
    ReadFilter<ReadIterator> a_read_filter {};
    
    a_read_filter.register_filter(is_not_secondary_alignment);
    a_read_filter.register_filter([] (const AlignedRead& the_read) {
        return is_good_mapping_quality(the_read, 20);
    });
    a_read_filter.register_filter([] (const AlignedRead& the_read) {
        return has_sufficient_good_quality_bases(the_read, 20, 10);
    });
    
    // context-based filters
    a_read_filter.register_filter(is_not_duplicate<ReadIterator>);
    
    std::vector<AlignedRead> good_reads {}, bad_reads {};
    good_reads.reserve(some_reads.size());
    bad_reads.reserve(some_reads.size());
    
    a_read_filter.filter_reads(std::make_move_iterator(some_reads.begin()),
                               std::make_move_iterator(some_reads.end()),
                               ContextBackInserter(good_reads),
                               ContextBackInserter(bad_reads));
    
    // TODO: check these numbers are actually correct!
    BOOST_CHECK(good_reads.size() == 649);
    BOOST_CHECK(bad_reads.size() == 20);
}

BOOST_AUTO_TEST_SUITE_END()

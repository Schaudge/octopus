// Copyright (c) 2015-2021 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef fasta_hpp
#define fasta_hpp

#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <memory>

#include <boost/filesystem/path.hpp>

#include "bioio.hpp"

#include "reference_reader.hpp"

namespace octopus {

class GenomicRegion;

namespace io {

class Fasta : public ReferenceReader
{
public:
    using Path = boost::filesystem::path;
    
    using ContigName      = ReferenceReader::ContigName;
    using GenomicSize     = ReferenceReader::GenomicSize;
    using GeneticSequence = ReferenceReader::GeneticSequence;
    
    struct Options
    {
        enum class CapitalisationPolicy { maintain, capitalise };
        enum class IUPACAmbiguitySymbolPolicy { maintain, disambiguate };
        enum class BaseFillPolicy { ignore, throw_exception, fill_with_ns };
        CapitalisationPolicy base_transform_policy = CapitalisationPolicy::maintain;
        IUPACAmbiguitySymbolPolicy iupac_ambiguity_symbol_policy = IUPACAmbiguitySymbolPolicy::maintain;
        BaseFillPolicy base_fill_policy = BaseFillPolicy::ignore;
    };
    
    Fasta() = delete;
    
    Fasta(Path fasta_path);
    Fasta(Path fasta_path, Options options);
    Fasta(Path fasta_path, Path fasta_index_path);
    Fasta(Path fasta_path, Path fasta_index_path, Options options);
    
    Fasta(const Fasta&);
    Fasta& operator=(Fasta);
    Fasta(Fasta&&)            = default;
    Fasta& operator=(Fasta&&) = default;
    
private:
    Path path_;
    Path index_path_;
    
    mutable std::ifstream fasta_;
    bioio::FastaIndex fasta_index_;
    
    Options options_;
    
    std::unique_ptr<ReferenceReader> do_clone() const override;
    bool do_is_open() const noexcept override;
    std::string do_fetch_reference_name() const override;
    std::vector<ContigName> do_fetch_contig_names() const override;
    GenomicSize do_fetch_contig_size(const ContigName& contig) const override;
    GeneticSequence do_fetch_sequence(const GenomicRegion& region) const override;
    
    bool is_valid_fasta() const noexcept;
    bool is_valid_fasta_index() const noexcept;
    bool is_capitalisation_requested() const noexcept;
};

} // namespace io
} // namespace octopus

#endif
